//-----------------------------------------------------------------------------
//
//	Main.cpp
//
//	Minimal application to test OpenZWave.
//
//	Creates an OpenZWave::Driver and the waits.  In Debug builds
//	you should see verbose logging to the console, which will
//	indicate that communications with the Z-Wave network are working.
//
//	Copyright (c) 2010 Mal Lansell <mal@openzwave.com>
//
//
//	SOFTWARE NOTICE AND LICENSE
//
//	This file is part of OpenZWave.
//
//	OpenZWave is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Lesser General Public License as published
//	by the Free Software Foundation, either version 3 of the License,
//	or (at your option) any later version.
//
//	OpenZWave is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Lesser General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with OpenZWave.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------
// Other modifications completed by conradvassallo.com, then thomasloughlin.com
// This version is by willemburgers.nl

//Open-Zwave includes:
#include <unistd.h>
#include <pthread.h>
#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Scene.h"
#include "Group.h"
#include "Notification.h"
#include "ValueStore.h"
#include "Value.h"
#include "ValueBool.h"
#include "ValueByte.h"
#include "ValueDecimal.h"
#include "ValueInt.h"
#include "ValueList.h"
#include "ValueShort.h"
#include "ValueString.h"

//External classes and libs
#include "Socket.h"
#include "SocketException.h"
#include "ProtocolException.h"
#include "sunrise.h"
#include "Configuration.h"
#include <libwebsockets.h>

//Necessary includes for Main
#include <time.h>
#include <string>
#include <iostream>
#include <stdio.h>
#include <vector>
#include <stdlib.h>
#include <sstream>
#include <stdexcept>
#include <signal.h>

#include "Main.h"
using namespace OpenZWave;

typedef struct {
	uint32			m_homeId;
	uint8			m_nodeId;
	//string			commandclass;
	uint8			basicmapping;
	time_t			m_LastSeen;
	bool			m_polled;
	list<ValueID>	m_values;
} NodeInfo;

struct Alarm {
	time_t			alarmtime;
	string			description;
	bool operator<(Alarm const &other)  { return alarmtime < other.alarmtime; }
	bool operator==(Alarm const &other)  { return alarmtime == other.alarmtime; }
};

static bool stopping = false;
static Socket* server;
static Configuration* conf;

static uint32 g_homeId = 0;
static bool g_initFailed = false;
static bool atHome = true;
static bool alarmset = false;
static list<Alarm> alarmlist;
static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t initCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

// Value-Defintions of the different String values
enum Commands {Undefined_command = 0, AList, Device, SetNode, SceneC, Create, Add, Remove, Activate, Cron, Switch, Test, AlarmList, ControllerC, Cancel, Exit};
enum Triggers {Undefined_trigger = 0, Sunrise, Sunset};
enum DeviceOptions {Undefined_Option = 0, Name, Location, Level, Polling, Battery_report};
static std::map<std::string, Commands> s_mapStringCommands;
static std::map<std::string, Triggers> s_mapStringTriggers;
static std::map<std::string, DeviceOptions> s_mapStringOptions;
static std::map<string, int> MapCommandClassBasic;

void create_string_maps() {
	s_mapStringCommands["ALIST"] = AList;
	s_mapStringCommands["DEVICE"] = Device;
	s_mapStringCommands["SETNODE"] = SetNode;
	s_mapStringCommands["SCENE"] = SceneC;
	s_mapStringCommands["CREATE"] = Create;
	s_mapStringCommands["ADD"] = Add;
	s_mapStringCommands["REMOVE"] = Remove;
	s_mapStringCommands["ACTIVATE"] = Activate;
	s_mapStringCommands["CRON"] = Cron;
	s_mapStringCommands["SWITCH"] = Switch;
	s_mapStringCommands["TEST"] = Test;
	s_mapStringCommands["ALARMLIST"] = AlarmList;
	s_mapStringCommands["CONTROLLER"] = ControllerC;
	s_mapStringCommands["CANCEL"] = Cancel;
	s_mapStringCommands["EXIT"] = Exit;
	
	s_mapStringTriggers["Sunrise"] = Sunrise;
	s_mapStringTriggers["Sunset"] = Sunset;
	
	s_mapStringOptions["Name"] = Name;
	s_mapStringOptions["Location"] = Location;
	s_mapStringOptions["Level"] = Level;
	s_mapStringOptions["Polling"] = Polling;
	s_mapStringOptions["Battery report"] = Battery_report;
	
	MapCommandClassBasic["0x03|0x11"] = 0x94;
	MapCommandClassBasic["0x03|0x12"] = 0x30;
	MapCommandClassBasic["0x08|0x02"] = 0x40;
	MapCommandClassBasic["0x08|0x03"] = 0x46;
	MapCommandClassBasic["0x08|0x04"] = 0x43;
	MapCommandClassBasic["0x08|0x05"] = 0x40;
	MapCommandClassBasic["0x08|0x06"] = 0x40;
	MapCommandClassBasic["0x09|0x01"] = 0x50;
	MapCommandClassBasic["0x10"] = 0x25;
	MapCommandClassBasic["0x11"] = 0x26;
	MapCommandClassBasic["0x12|0x01"] = 0x25;
	MapCommandClassBasic["0x12|0x02"] = 0x26;
	MapCommandClassBasic["0x12|0x03"] = 0x28;
	MapCommandClassBasic["0x12|0x04"] = 0x29;
	MapCommandClassBasic["0x13|0x01"] = 0x28;
	MapCommandClassBasic["0x13|0x02"] = 0x29;
	MapCommandClassBasic["0x16|0x01"] = 0x39;
	MapCommandClassBasic["0x20"] = 0x30;
	MapCommandClassBasic["0x21"] = 0x31;
	MapCommandClassBasic["0x30"] = 0x35;
	MapCommandClassBasic["0x31|0x01"] = 0x32;
	MapCommandClassBasic["0x40|0x01"] = 0x62;
	MapCommandClassBasic["0x40|0x02"] = 0x62;
	MapCommandClassBasic["0x40|0x03"] = 0x62;
	MapCommandClassBasic["0xa1"] = 0x71;
}

//functions
void *websockets_main(void* arg);
void *run_socket(void* arg);
std::string process_commands(std::string data);
bool SetValue(int32 home, int32 node, int32 value, uint8 cmdclass, std::string& err_message);
std::string switchAtHome();
std::string activateScene(string sclabel);
bool parse_option(int32 home, int32 node, std::string name, std::string value, bool& save, std::string& err_message);
bool try_map_basic(int32 home, int32 node);
void sigalrm_handler(int sig);

//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------

NodeInfo* GetNodeInfo(uint32 const homeId, uint8 const nodeId) {
	for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
		NodeInfo* nodeInfo = *it;
		if((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
			return nodeInfo;
		}
	}

	return NULL;
}

NodeInfo* GetNodeInfo(Notification const* notification) {
	uint32 const homeId = notification->GetHomeId();
	uint8 const nodeId = notification->GetNodeId();
	return GetNodeInfo(homeId, nodeId);
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------

void OnNotification(Notification const* _notification, void* _context) {
	// Must do this inside a critical section to avoid conflicts with the main thread
	pthread_mutex_lock(&g_criticalSection);

	switch(_notification->GetType()) {
		case Notification::Type_ValueAdded:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				// Add the new value to our list
				nodeInfo->m_values.push_back( _notification->GetValueID());
			}
			break;
		}

		case Notification::Type_ValueRemoved:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				// Remove the value from out list
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
					if((*it) == _notification->GetValueID()) {
						nodeInfo->m_values.erase(it);
						break;
					}
				}
			}
			break;
		}

		case Notification::Type_ValueChanged:
		{
			// One of the node values has changed
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time( NULL );
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
                    if((*it) == _notification->GetValueID()) {
                        nodeInfo->m_values.erase(it);
                        break;
                    }
                }
				nodeInfo->m_values.push_back(_notification->GetValueID());
				//Todo: clean up this update. This was a fast way to update the status
			}
			break;
		}

		case Notification::Type_Group:
		{
			// One of the node's association groups has changed
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				nodeInfo = nodeInfo;            // placeholder for real action
			}
			break;
		}

		case Notification::Type_NodeAdded:
		{
			// Add the new node to our list
			NodeInfo* nodeInfo = new NodeInfo();
			nodeInfo->m_homeId = _notification->GetHomeId();
			nodeInfo->m_nodeId = _notification->GetNodeId();
			nodeInfo->m_polled = false;
			g_nodes.push_back(nodeInfo);
			break;
		}

		case Notification::Type_NodeRemoved:
		{
			// Remove the node from our list
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				NodeInfo* nodeInfo = *it;
				if((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
					g_nodes.erase(it);
					delete nodeInfo;
					break;
				}
			}
			break;
		}

		case Notification::Type_NodeEvent:
		{
			// We have received an event from the node, caused by a
			// basic_set or hail message.
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time( NULL );
			}
			break;
		}

		case Notification::Type_PollingDisabled:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_polled = false;
			}
			break;
		}

		case Notification::Type_PollingEnabled:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_polled = true;
			}
			break;
		}

		case Notification::Type_DriverReady:
		{
			g_homeId = _notification->GetHomeId();
			break;
		}

		case Notification::Type_DriverFailed:
		{
			g_initFailed = true;
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_AwakeNodesQueried:
		case Notification::Type_AllNodesQueried:
		case Notification::Type_AllNodesQueriedSomeDead:
		{
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_NodeProtocolInfo:
		{
			char buffer[10];
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
			if(NodeInfo* nodeInfo = GetNodeInfo(homeId, nodeId)) {
				
				uint8 generic = Manager::Get()->GetNodeGeneric(homeId , nodeId);
				uint8 specific = Manager::Get()->GetNodeSpecific(homeId, nodeId);
				
				snprintf(buffer, 10, "0x%02X|0x%02X", generic, specific);
				if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
					nodeInfo->basicmapping = MapCommandClassBasic[buffer];
				}
				else {
					// We didn't find a Generic+Specifc in the table, now we check
					// for Generic only
					snprintf(buffer, 10, "0x%02X", generic);

					// Check if we have a mapping in our map table
					if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
						nodeInfo->basicmapping = MapCommandClassBasic[buffer];
					}
				}
			}
		}
		case Notification::Type_DriverReset:
		case Notification::Type_Notification:
		case Notification::Type_NodeNaming:
		case Notification::Type_NodeQueriesComplete:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time( NULL );
			}
			break;
		}
		default:
		{
		}
	}

	pthread_mutex_unlock(&g_criticalSection);
}

void OnControllerUpdate( Driver::ControllerState cs, Driver::ControllerError err, void *ct ) {
	//m_structCtrl *ctrl = (m_structCtrl *)ct;

	// Possible ControllerState values:
	// ControllerState_Normal     - No command in progress.
	// ControllerState_Starting   - The command is starting.
	// ControllerState_Cancel     - The command was cancelled.
	// ControllerState_Error      - Command invocation had error(s) and was aborted.
	// ControllerState_Sleeping   - Controller command is on a sleep queue wait for device.
	// ControllerState_Waiting    - Controller is waiting for a user action.
	// ControllerState_InProgress - The controller is communicating with the other device to carry out the command.
	// ControllerState_Completed  - The command has completed successfully.
	// ControllerState_Failed     - The command has failed.
	// ControllerState_NodeOK     - Used only with ControllerCommand_HasNodeFailed to indicate that the controller thinks the node is OK.
	// ControllerState_NodeFailed - Used only with ControllerCommand_HasNodeFailed to indicate that the controller thinks the node has failed.

	pthread_mutex_lock( &g_criticalSection );

	switch (cs) {
		case Driver::ControllerState_Normal:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Normal - no command in progress", ctrl->m_homeId );
			std::cout << "ControllerState: Normal" << endl;
			//ctrl->m_controllerBusy = false;
			break;
		}
		case Driver::ControllerState_Starting:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Starting - the command is starting", ctrl->m_homeId );
			std::cout << "ControllerState: Starting" << endl;
			break;
		}
		case Driver::ControllerState_Cancel:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Cancel - the command was cancelled", ctrl->m_homeId );
			std::cout << "ControllerState: Cancel" << endl;
			break;
		}
		case Driver::ControllerState_Error:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Error - command invocation had error(s) and was aborted", ctrl->m_homeId );
			std::cout << "ControllerState: Error" << endl;
			break;
		}
		case Driver::ControllerState_Sleeping:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Sleeping - controller command is on a sleep queue wait for device", ctrl->m_homeId );
			std::cout << "ControllerState: Sleeping" << endl;
			break;
		}
		case Driver::ControllerState_Waiting:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Waiting - waiting for a user action", ctrl->m_homeId );
			std::cout << "ControllerState: Waiting" << endl;
			break;
		}
		case Driver::ControllerState_InProgress:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - InProgress - communicating with the other device", ctrl->m_homeId );
			std::cout << "ControllerState: InProgress" << endl;
			break;
		}
		case Driver::ControllerState_Completed:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Completed - command has completed successfully", ctrl->m_homeId );
			std::cout << "ControllerState: Completed" << endl;
			//ctrl->m_controllerBusy = false;
			break;
		}
		case Driver::ControllerState_Failed:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - Failed - command has failed", ctrl->m_homeId );
			std::cout << "ControllerState: Failed" << endl;
			//ctrl->m_controllerBusy = false;
			break;
		}
		case Driver::ControllerState_NodeOK:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - NodeOK - the node is OK", ctrl->m_homeId );
			std::cout << "ControllerState: NodeOk" << endl;
			//ctrl->m_controllerBusy = false;

			// Store Node State

			break;
		}
		case Driver::ControllerState_NodeFailed:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - NodeFailed - the node has failed", ctrl->m_homeId );
			std::cout << "ControllerState: NodeFailed" << endl;
			//ctrl->m_controllerBusy = false;

			// Store Node State

			break;
		}
		default:
		{
			//WriteLog( LogLevel_Debug, true, "ControllerState Event: HomeId=%d - unknown response", ctrl->m_homeId );
			std::cout << "ControllerState: Unknown" << endl;
			//ctrl->m_controllerBusy = false;
			break;
		}
	}

	// Additional possible error information
	switch (err) {
		case Driver::ControllerError_None:
		{
			//WriteLog( LogLevel_Debug, false, "Error=None" );
			break;
		}
		case Driver::ControllerError_ButtonNotFound:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Button Not Found" );
			std::cout << "ControllerError: Button Not Found" << endl;
			break;
		}
		case Driver::ControllerError_NodeNotFound:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Node Not Found" );
			std::cout << "ControllerError: Node Not Found" << endl;
			break;
		}
		case Driver::ControllerError_NotBridge:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not a Bridge" );
			std::cout << "ControllerError: Not a Bridge" << endl;
			break;
		}
		case Driver::ControllerError_NotPrimary:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not Primary Controller" );
			std::cout << "ControllerError: Not Primary Controller" << endl;
			break;
		}
		case Driver::ControllerError_IsPrimary:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Is Primary Controller" );
			std::cout << "ControllerError: Is Primary Controller" << endl;
			break;
		}
		case Driver::ControllerError_NotSUC:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not Static Update Controller" );
			std::cout << "ControllerError: Not Static Update Controller" << endl;
			break;
		}
		case Driver::ControllerError_NotSecondary:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not Secondary Controller" );
			std::cout << "ControllerError: Not Secondary Controller" << endl;
			break;
		}
		case Driver::ControllerError_NotFound:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Not Found" );
			std::cout << "ControllerError: Not Found" << endl;
			break;
		}
		case Driver::ControllerError_Busy:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Controller Busy" );
			std::cout << "ControllerError: Busy" << endl;
			break;
		}
		case Driver::ControllerError_Failed:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Failed" );
			std::cout << "ControllerError: Failed" << endl;
			break;
		}
		case Driver::ControllerError_Disabled:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Disabled" );
			std::cout << "ControllerError: Disabled" << endl;
			break;
		}
		case Driver::ControllerError_Overflow:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Overflow" );
			std::cout << "ControllerError: Overflow" << endl;
			break;
		}
		default:
		{
			//WriteLog( LogLevel_Debug, false, "Error=Unknown error (%d)", err );
			std::cout << "ControllerError: Unknown error" << endl;
			break;
		}
	}

	pthread_mutex_unlock( &g_criticalSection );

	// If the controller isn't busy anymore and we still got something in the queue, fire off the command now
	/*if ( ctrl->m_controllerBusy == false )
	{
		if ( ! ctrl->m_cmd.empty() )
		{
			bool response;
			m_cmdItem cmd;

			cmd = ctrl->m_cmd.front();
			ctrl->m_cmd.pop_front();

			// Now start the BeginControllerCommand with the 2 supported options
			switch( cmd.m_command ) {
				case Driver::ControllerCommand_HasNodeFailed:
				{
					//ctrl->m_controllerBusy = true;
					//WriteLog( LogLevel_Debug, true, "DomoZWave_HasNodeFailed: HomeId=%d Node=%d (Queued)", ctrl->m_homeId, cmd.m_nodeId );
					response = Manager::Get()->BeginControllerCommand( ctrl->m_homeId, Driver::ControllerCommand_HasNodeFailed, OnControllerUpdate, ctrl, true, cmd.m_nodeId );
		                        //WriteLog( LogLevel_Debug, false, "Return=%s", (response)?"CommandSend":"ControllerBusy" );
					break;
				}
				case Driver::ControllerCommand_RequestNodeNeighborUpdate:
				{
					//ctrl->m_controllerBusy = true;
					//WriteLog( LogLevel_Debug, true, "DomoZWave_RequestNodeNeighborUpdate: HomeId=%d Node=%d (Queued)", ctrl->m_homeId, cmd.m_nodeId );
					response = Manager::Get()->BeginControllerCommand( ctrl->m_homeId, Driver::ControllerCommand_RequestNodeNeighborUpdate, OnControllerUpdate, ctrl, false, cmd.m_nodeId );
		                        //WriteLog( LogLevel_Debug, false, "Return=%s", (response)?"CommandSend":"ControllerBusy" );
					break;
				}
				default:
				{
					//WriteLog( LogLevel_Debug, true, "DomoZWave_OnControllerUpdate: HomeId=%d Node=%d (Queued)", ctrl->m_homeId, cmd.m_nodeId );
					//WriteLog( LogLevel_Debug, false, "ERROR: Invalid Command %d", cmd.m_command );
					break;
				}
			}
		}
	}*/
}

//-----------------------------------------------------------------------------
// Libwebsockets definitions
//-----------------------------------------------------------------------------

// struct for the thread data
struct analyze_data_info {  
    unsigned char *data;
    struct libwebsocket *wsi;
	size_t len;
};

// def thread count
#define THREADS 1000

// keep track of our threads
static int current_thread = 0;  
static pthread_t thread[THREADS];

// method called by thread to analyze data
void *analyzeThread(void *in) {
    // get the data from our struct
    struct analyze_data_info *info = (struct analyze_data_info*)in;

    // store the data as a char
    unsigned char *data = info->data;
	size_t len = info->len;

    // send the response
    libwebsocket_write(info->wsi, data, len, LWS_WRITE_TEXT);

    return NULL;
}

static int nullHttpCallback(struct libwebsocket_context *context,
							struct libwebsocket *wsi,
							enum libwebsocket_callback_reasons reason,
							void *user, void *in, size_t len) {
    return 0;
}

static int echoCallback(struct libwebsocket_context *context,
									struct libwebsocket *wsi,
									enum libwebsocket_callback_reasons reason,
									void *user, void *in, size_t len) {
	// reason for callback
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED:
			printf("connection established\n");
			break;

		case LWS_CALLBACK_RECEIVE: {
			// create a struct with the data
			struct analyze_data_info ainfo;
			ainfo.data = (unsigned char*)in;
			ainfo.wsi = wsi;
			ainfo.len = len;

			// analyze what was sent (launch a thread to do so)
			pthread_create(&thread[++current_thread], NULL, analyzeThread, (void *)&ainfo);

			// log what we recieved and what we're going to send as a response.
			printf("received data: %s\n", (char *) in);
			break;
		}
		default:
		break;
	}
	return 0;
}

// protocol types for websockets
static struct libwebsocket_protocols protocols[] = {  
    {
        "http-only",
        nullHttpCallback,
        0
    },
    {
        "echo",
        echoCallback,
        0
    },
    {
        NULL, NULL, 0
    }
};

/******** DOSTUFF() *********************
 There is a separate instance of this function 
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/

void split(const string& s, const string& delimiter, vector<string>& v) {
	string::size_type i = 0;
	string::size_type j = s.find(delimiter);
	while (j != string::npos) {
		v.push_back(s.substr(i, j - i));
		i = j += delimiter.length();
		j = s.find(delimiter, j);
	}
	v.push_back(s.substr(i, s.length()));
}

string trim(string s) {
	return s.erase(s.find_last_not_of(" \n\r\t") + 1);
}

template <typename T>
T lexical_cast(const std::string& s) {
	std::stringstream ss(s);

	T result;
	if((ss >> result).fail() || !(ss >> std::ws).eof())
	{
		throw std::runtime_error("Bad cast");
	}

	return result;
}

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
	conf = new Configuration();
	pthread_mutexattr_t mutexattr;

	pthread_mutexattr_init(&mutexattr);
	pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&g_criticalSection, &mutexattr);
	pthread_mutexattr_destroy(&mutexattr);

	pthread_mutex_lock(&initMutex);

	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file. If you leave it NULL
	// the log file will appear in the program's working directory.
	//Options::Create("../../../../config/", "", "");
	Options::Create("./config/", "", "");
	Options::Get()->Lock();

	Manager::Create();

    // Add a callback handler to the manager.  The second argument is a context that
    // is passed to the OnNotification method.  If the OnNotification is a method of
    // a class, the context would usually be a pointer to that class object, to
    // avoid the need for the notification handler to be a static.
    Manager::Get()->AddWatcher(OnNotification, NULL);

    // Add a Z-Wave Driver
    // Modify this line to set the correct serial port for your PC interface.

    string port = "/dev/ttyUSB0";

    Manager::Get()->AddDriver((argc > 1) ? argv[1] : port);
    //Manager::Get()->AddDriver( "HID Controller", Driver::ControllerInterface_Hid );

    // Now we just wait for the driver to become ready, and then write out the loaded config.
    // In a normal app, we would be handling notifications and building a UI for the user.
	
	Manager::Get()->SetPollInterval(1000*60*15, false); //15 minutes
	
    pthread_cond_wait(&initCond, &initMutex);

    if(!g_initFailed) {
	
		create_string_maps();
		Manager::Get()->WriteConfig(g_homeId);

		Driver::DriverData data;
		Manager::Get()->GetDriverStatistics(g_homeId, &data);

		printf("SOF: %d ACK Waiting: %d Read Aborts: %d Bad Checksums: %d\n", data.m_SOFCnt, data.m_ACKWaiting, data.m_readAborts, data.m_badChecksum);
		printf("Reads: %d Writes: %d CAN: %d NAK: %d ACK: %d Out of Frame: %d\n", data.m_readCnt, data.m_writeCnt, data.m_CANCnt, data.m_NAKCnt, data.m_ACKCnt, data.m_OOFCnt);
		printf("Dropped: %d Retries: %d\n", data.m_dropped, data.m_retries);
		printf("***************************************************** \n");
		printf("6004 ZWaveCommander Server \n");
		
		//start the websocket in a new thread
		pthread_t websocket_thread;
		if(pthread_create(&websocket_thread , NULL ,  websockets_main ,NULL) < 0) {
			throw std::runtime_error("Unable to create thread");
		}
		else {
			std::cout<< "Websocket starting" << endl;
		}
		
		while(!stopping) {
			try { // for all socket errors
				server = new Socket();
				if(!server->create()) {
					throw SocketException ( "Could not create server socket." );
				}
				if(!server->bind(6004)) {
					throw SocketException ( "Could not bind to port." );
				}
				if(!server->listen()) {
					throw SocketException ( "Could not listen to socket." );
				}
				Socket new_sock;
				while(server->accept(new_sock)) {
					pthread_t thread;
					int thread_sock2;
					thread_sock2 = new_sock.GetSock();
					if(pthread_create(&thread , NULL ,  run_socket ,(void*) thread_sock2) < 0) {
						throw std::runtime_error("Unable to create thread");
					}
					else {
						std::cout<< "Connection Created" << endl;
					}
				}
			}
			catch (SocketException& e) {
				std::cout << "SocketException: " << e.what() << endl;
			}
			catch(...) {
				std::cout << "Other exception" << endl;
			}
			std::cout << "Caught an exception, resolve the issue and press ENTER to continue" << endl;
			std::cin.ignore();
		}
    }
	
	// program exit (clean up)
	std::cout << "Closing connection to Zwave Controller" << endl;
	
	if(strcasecmp(port.c_str(), "usb") == 0) {
		Manager::Get()->RemoveDriver("HID Controller");
	}
	else {
		Manager::Get()->RemoveDriver(port);
	}
	Manager::Get()->RemoveWatcher(OnNotification, NULL);
	Manager::Destroy();
	Options::Destroy();
	pthread_mutex_destroy(&g_criticalSection);
	return 0;
}

void *websockets_main(void* arg) {
	// server url will be ws://localhost:9000
    int port = 9000;
    const char *interface = NULL;
    struct libwebsocket_context *context;

    // we're not using ssl
    const char *cert_path = NULL;
    const char *key_path = NULL;

    // no special options
    int opts = 0;

    // create connection struct
    struct lws_context_creation_info info;
    info.port = port;
    info.iface = interface;
    info.protocols = protocols;
    info.extensions = NULL;
    info.ssl_cert_filepath = cert_path;
    info.ssl_private_key_filepath = key_path;
    info.options = opts;

    // create libwebsocket context representing this server
    context = libwebsocket_create_context(&info);

    // make sure it starts
    if(context == NULL) {
        fprintf(stderr, "libwebsocket init failed\n");
        return 0;
    }
    printf("starting server...\n");

    // infinite loop, to end this server send SIGTERM. (CTRL+C)
    while (1) {
        libwebsocket_service(context, 10);
    }
    libwebsocket_context_destroy(context);
}

void *run_socket(void* arg) {
	Socket thread_sock;
	thread_sock.SetSock((int)arg);
	while(true) {
		try { // command parsing errors
			//get commands from the socket
			std::string data;
			thread_sock >> data;
			
			if(strcmp(data.c_str(), "") == 0){ //client closed the connection
				std::cout << "Client closed the connection" << endl;
				return 0;
			}
			
			thread_sock << process_commands(data);
		}
		catch (ProtocolException& e) {
			string what = "ProtocolException: ";
			what += e.what();
			what += "\n";
			thread_sock << what;
		}
		catch (std::exception const& e) {
			std::cout << "Exception: " << e.what() << endl;
		}
		catch (SocketException& e) {
			std::cout << "SocketException: " << e.what() << endl;
		}
	}
}

std::string process_commands(std::string data) {
	vector<string> v;
	split(data, "~", v);
	string output = "";
	switch (s_mapStringCommands[trim(v[0].c_str())])
	{
		case AList:
		{
			string device;
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				NodeInfo* nodeInfo = *it;
				int nodeID = nodeInfo->m_nodeId;
				
				string nodeType = Manager::Get()->GetNodeType(g_homeId, nodeInfo->m_nodeId);
				string nodeName = Manager::Get()->GetNodeName(g_homeId, nodeInfo->m_nodeId);
				string nodeZone = Manager::Get()->GetNodeLocation(g_homeId, nodeInfo->m_nodeId);
				string nodeValue ="";	//(string) Manager::Get()->RequestNodeState(g_homeId, nodeInfo->m_nodeId);
										//The point of this was to help me figure out what the node values looked like
				for(list<ValueID>::iterator it5 = nodeInfo->m_values.begin(); it5 != nodeInfo->m_values.end(); ++it5) {
					string tempstr="";
					Manager::Get()->GetValueAsString(*it5,&tempstr);                   
					tempstr= "="+tempstr;
					//hack to delimit values .. need to properly escape all values
					nodeValue+="<>"+ Manager::Get()->GetValueLabel(*it5) +tempstr;
				}

				if(nodeName.size() == 0)
					nodeName = "Undefined";

				if(nodeType != "Static PC Controller" && nodeType != "") {
					stringstream ssNodeName, ssNodeId, ssNodeType, ssNodeZone, ssNodeLastSeen, ssNodeValue;
					ssNodeName << nodeName;
					ssNodeId << nodeID;
					ssNodeType << nodeType;
					ssNodeZone << nodeZone;
					ssNodeLastSeen << ctime(&(nodeInfo->m_LastSeen));
					ssNodeValue << nodeValue;
					device += "DEVICE~" + ssNodeName.str() + "~" + ssNodeId.str() + "~"+ ssNodeZone.str() +"~" + ssNodeType.str() + "~" + ssNodeLastSeen.str() + "~" + ssNodeValue.str() + "#";
				}
			}
			device = device.substr(0, device.size() - 1) + "\n";                           
			std::cout << "Sent Device List \n";
			output += device;
			break;
		}
		case Device:
		{
			/*if(v.size() != 3) {
				throw ProtocolException(2, "Wrong number of arguments");
			}
			
			int Node = 0;
			int Level = 0;
			std::string err_message = "";

			Node = lexical_cast<int>(v[1].c_str());
			Level = lexical_cast<int>(v[2].c_str());
			
			if(!SetValue(g_homeId, Node, Level, err_message)){
				output += err_message;
			}
			else{
				stringstream ssNode, ssLevel;
				ssNode << Node;
				ssLevel << Level;
				output += "MSG~ZWave Node=" + ssNode.str() + " Level=" + ssLevel.str() + "\n";
			}
			break;*/
		}
		case SetNode:
		{
			if(v.size() != 3) {
				throw ProtocolException(2, "Wrong number of arguments");
			}
			int Node = 0;
			string Options = "";
			
			Node = lexical_cast<int>(v[1].c_str());
			Options=trim(v[2].c_str());
			
			if(!Options.empty()) {
				vector<string> OptionList;
				split(Options, "<>", OptionList);
				bool save = false;
				
				for(std::vector<string>::iterator it = OptionList.begin(); it != OptionList.end(); ++it) {
					std::cout << (*it) << endl;
					std::size_t found = (*it).find('=');
					if(found!=std::string::npos) {
						std::string name = (*it).substr(0,found);
						std::string value = (*it).substr(found+1);
						std::string err_message = "";
						if(!parse_option(g_homeId, Node, name, value, save, err_message)) {
							output += "Error while parsing options\n";
							output += err_message;
							break;
						}
					}
				}
				if(save) {
					//save details to XML
					Manager::Get()->WriteConfig(g_homeId);
				}
			}
			
			stringstream ssNode;
			ssNode << Node;
			output += "MSG~ZWave Name set Node=" + ssNode.str() + "\n";
			
			break;
		}
		case SceneC:
		{
			if(v.size() < 3) {
				throw ProtocolException(2, "Wrong number of arguments");
			}
			switch(s_mapStringCommands[trim(v[1].c_str())])
			{
				case Create:
				{
					string sclabel = trim(v[2].c_str());
					if(int scid = Manager::Get()->CreateScene()) {
						stringstream ssID;
						ssID << scid;
						Manager::Get()->SetSceneLabel(scid, sclabel);
						output += "Scene created with name " + sclabel +" and scene_id " + ssID.str() + "\n";
					}
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Add:
				{
					if(v.size() != 5) {
						throw ProtocolException(2, "Wrong number of arguments");
					}
					uint8 numscenes = 0;
					uint8 *sceneIds = new uint8[numscenes];
					
					if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
						throw ProtocolException(3, "No scenes created");
					}
					
					stringstream ssNum;
					ssNum << numscenes;
					output += "numscenes " + ssNum.str() + "\n";
					
					string sclabel = trim(v[2].c_str());
					int scid=0;
					int Node = lexical_cast<int>(v[3].c_str());
					int Level = lexical_cast<int>(v[4].c_str());
					
					for(int i=0; i<numscenes; ++i) {
						scid = sceneIds[i];
						if(sclabel != Manager::Get()->GetSceneLabel(scid)) {
							continue;
						}
						output += "Found right scene\n";
						NodeInfo* nodeInfo = GetNodeInfo(g_homeId, Node);
						for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
							int id = (*vit).GetCommandClassId();
							string vlabel = Manager::Get()->GetValueLabel( (*vit) );
						
							if(id!=COMMAND_CLASS_SWITCH_MULTILEVEL || vlabel != "Level") {
								continue;
							}
							output += "Add valueid/value to scene\n";
							Manager::Get()->AddSceneValue(scid, (*vit), Level);
						}
					}
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Remove:
				{
					if(v.size() != 4) {
						throw ProtocolException(2, "Wrong number of arguments");
					}
					uint8 numscenes = 0;
					uint8 *sceneIds = new uint8[numscenes];
					
					if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
						throw ProtocolException(3, "No scenes created");
					}
					
					stringstream ssNum;
					ssNum << numscenes;
					output += "numscenes " + ssNum.str() + "\n";
					
					string sclabel = trim(v[2].c_str());
					int scid=0;
					int Node = lexical_cast<int>(v[3].c_str());
					
					for(int i=0; i<numscenes; ++i){
						scid = sceneIds[i];
						
						if(sclabel != Manager::Get()->GetSceneLabel(scid)){
							continue;
						}
						output += "Found right scene\n";
						NodeInfo* nodeInfo = GetNodeInfo(g_homeId, Node);
						for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
							int id = (*vit).GetCommandClassId();
							string vlabel = Manager::Get()->GetValueLabel( (*vit) );
						
							if(id!=COMMAND_CLASS_SWITCH_MULTILEVEL || vlabel != "Level") {
								continue;
							}
							output += "Remove valueid from scene\n";
							Manager::Get()->RemoveSceneValue(scid, (*vit));
						}
					}
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Activate:
				{
					output += activateScene(v[2].c_str());
					break;
				}
				default:
					throw ProtocolException(1, "Unknown Scene command");
					break;
			}
			break;
		}
		case Cron:
		{
			//planning to add a google calendar add-in here.
			//right now it will just update the sunrise and sunset times for today
			//call zcron.sh from cron to enable this function
			
			time_t sunrise = 0, sunset = 0;
			float lat, lon;
			conf->GetLocation(lat, lon);
			if(GetSunriseSunset(sunrise,sunset,lat,lon)) {
				Alarm sunriseAlarm;
				Alarm sunsetAlarm;
				sunriseAlarm.alarmtime = sunrise;
				sunsetAlarm.alarmtime = sunset;
				sunriseAlarm.description = "Sunrise";
				sunsetAlarm.description = "Sunset";
				
				alarmlist.push_back(sunriseAlarm);
				alarmlist.push_back(sunsetAlarm);
			}
			
			alarmlist.sort();
			alarmlist.unique();
			
			time_t now = time(NULL);
			
			while(!alarmlist.empty() && (alarmlist.front().alarmtime) <= now)
			{alarmlist.pop_front();}
			
			if(!alarmlist.empty() && !alarmset && (alarmlist.front().alarmtime > now)) {
				signal(SIGALRM, sigalrm_handler);   
				alarm(alarmlist.front().alarmtime - now);
				alarmset = true;
			}
			break;
		}
		case Switch:
		{
			output += switchAtHome();
			break;
		}
		case Test:
		{
			/*string response = "false";
			if(Manager::Get()->BeginControllerCommand( g_homeId, Driver::ControllerCommand_AddDevice, OnControllerUpdate))
				response = "true";
			output += response;*/
			
			break;
		}
		case AlarmList:
			for(list<Alarm>::iterator it = alarmlist.begin(); it!=alarmlist.end(); it++) {
				stringstream ssTime;
				ssTime << ctime(&(it->alarmtime));
				output += ssTime.str();
			}
			break;
		case Exit:
			stopping = true;
			delete server;
			break;
		default:
			throw ProtocolException(1, "Unknown command");
			break;
	}
	return output;
}

bool SetValue(int32 home, int32 node, int32 value, uint8 cmdclass, std::string label, std::string& err_message) {
	err_message = "";
	bool bool_value;
	int int_value;
	uint8 uint8_value;
	uint16 uint16_value;
	bool response;
	bool cmdfound = false;
	
	if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {
		// Find the correct instance
		for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
			int id = (*it).GetCommandClassId();
			if(id != cmdclass) {
				continue;
			}

			if(label != Manager::Get()->GetValueLabel(*it)) {
				continue;
			}
			
			if(ValueID::ValueType_Bool == (*it).GetType()) {
				bool_value = (bool)value;
				response = Manager::Get()->SetValue( *it, bool_value );
				cmdfound = true;
			}
			else if(ValueID::ValueType_Byte == (*it).GetType()) {
				uint8_value = (uint8)value;
				response = Manager::Get()->SetValue( *it, uint8_value );
				cmdfound = true;
			}
			else if(ValueID::ValueType_Short == (*it).GetType()) {
				uint16_value = (uint16)value;
				response = Manager::Get()->SetValue( *it, uint16_value );
				cmdfound = true;
			}
			else if(ValueID::ValueType_Int == (*it).GetType()) {
				int_value = value;
				response = Manager::Get()->SetValue( *it, int_value );
				cmdfound = true;
			}
			else if(ValueID::ValueType_List == (*it).GetType()) {
				response = Manager::Get()->SetValue( *it, value );
				cmdfound = true;
			}
			else {
				err_message += "unknown ValueType | ";
				return false;
			}
		}

		if(!cmdfound) {
			err_message += "Couldn't match node to the required COMMAND_CLASS_SWITCH_BINARY or COMMAND_CLASS_SWITCH_MULTILEVEL\n";
			return false;
		}
	}
	else {
		//WriteLog( LogLevel_Debug, false, "Return=false (node doesn't exist)" );
		err_message += "node doesn't exist";
		response = false;
	}

	return response;
}

std::string switchAtHome() {
	time_t sunrise = 0, sunset = 0;
	float lat, lon;
	conf->GetLocation(lat, lon);
	std::string output = "";
	if(GetSunriseSunset(sunrise,sunset,lat,lon)) {
		atHome = !atHome;
		if(atHome) {
			output += "Welcome home\n";
			time_t now = time(NULL);
			if(now > sunrise && now < sunset) {
				// turn on the athome scene set by the user for the day
				std::string dayScene;
				conf->GetDayScene(dayScene);
				try {
					output += activateScene(dayScene);
				}
				catch (ProtocolException& e) {
					std::string what = "ProtocolException: ";
					what += e.what();
					output += what + "\n";
					output += "No dayScene is set, set it in Config.ini\n";
				}
			}
			else {
				// turn on the athome scene set by the user for the evening/night
				std::string nightScene;
				conf->GetNightScene(nightScene);
				try {
					output += activateScene(nightScene);
				}
				catch (ProtocolException& e) {
					std::string what = "ProtocolException: ";
					what += e.what();
					output += what + "\n";
					output += "No nightScene is set, set it in Config.ini\n";
				}
			}
		}
		else {
			output += "Bye bye\n";
			std::string awayScene;
			conf->GetAwayScene(awayScene);
			try {
				output += activateScene(awayScene);
			}
			catch (ProtocolException& e) {
				string what = "ProtocolException: ";
				what += e.what();
				output += what + "\n";
				output += "No awayScene is set, set it in Config.ini\n";
			}
		}
	}
	return output;
}

std::string activateScene(std::string sclabel) {
	uint8 numscenes = 0;
	uint8 *sceneIds = new uint8[numscenes];
	
	sclabel = trim(sclabel);
	
	if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
		throw ProtocolException(3, "No scenes created");
	}
	
	int scid=0;
	
	for(int i=0; i<numscenes; ++i) {
		scid = sceneIds[i];
		if(sclabel != Manager::Get()->GetSceneLabel(scid)){
			continue;
		}
		Manager::Get()->ActivateScene(scid);
		return "Activate scene "+sclabel+"\n";
	}
	throw ProtocolException(4, "Scene not found");
}

bool parse_option(int32 home, int32 node, std::string name, std::string value, bool& save, std::string& err_message) {
	err_message = "";
	switch(s_mapStringOptions[name])
	{
		case Name:
		{
			pthread_mutex_lock(&g_criticalSection);
			Manager::Get()->SetNodeName(home, node, value);
			pthread_mutex_unlock(&g_criticalSection);
			save = true;
			return true;
			break;
		}
		case Location:
		{
			pthread_mutex_lock(&g_criticalSection);
			Manager::Get()->SetNodeLocation(home, node, value);
			pthread_mutex_unlock(&g_criticalSection);
			save = true;
			return true;
			break;
		}
		case Level:
		{
			uint8 cmdclass = COMMAND_CLASS_SWITCH_MULTILEVEL;
			return SetValue(home, node, lexical_cast<int>(value), cmdclass, "Level", err_message);
			break;
		}
		case Polling:
		{
			bool found = false;
			if(Manager::Get()->GetNodeBasic( home, node ) < 0x03) {
				err_message += "Node is a controller\n";
				return false;
			}
			if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {
				uint8 cmdclass = 0;
				if(nodeInfo->basicmapping > 0 || try_map_basic(home, node)) {
					cmdclass = nodeInfo->basicmapping;
					std::cout << "mapped to " << (int) cmdclass << endl;
				}
				else {
					cmdclass = COMMAND_CLASS_BASIC;
					std::cout << "mapped to BASIC" << endl;
				}
				
				// Mark the basic command class values for polling
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it){
					if((*it).GetCommandClassId() == cmdclass) {
						// It works fine, EXCEPT for MULTILEVEL, then we need to ignore all except the first one
						if((*it).GetCommandClassId() == COMMAND_CLASS_SWITCH_MULTILEVEL) {
							if((*it).GetIndex() != 0) {
								continue;
							}
						}
						if(lexical_cast<bool>(value)) {
							if(!Manager::Get()->EnablePoll(*it)) {
								err_message += "Could not enable polling for this value\n";
								return false;
							}
						}
						else {
							if(!Manager::Get()->DisablePoll(*it)) {
								err_message += "Could not disable polling for this value\n";
								return false;
							}
						}
						if(!found)
						{
							found = true;
						}
					}
				}
				if(!found) {
					err_message += "Node does not have COMMAND_CLASS_BASIC\n";
					return false;
				}
			}
			save = true;
			return true;
			break;
		}
		case Battery_report:
		{
			uint8 cmdclass = COMMAND_CLASS_CONFIGURATION;
			save = true;
			return SetValue(home, node, lexical_cast<int>(value), cmdclass, "Send unsolicited battery report on wakeup", err_message);
		}
		default:
		break;
	}
	return false;
}

bool try_map_basic(int32 home, int32 node) {
	char buffer[10];
	if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {					
		uint8 generic = Manager::Get()->GetNodeGeneric(home, node);
		uint8 specific = Manager::Get()->GetNodeSpecific(home, node);			
		
		snprintf(buffer, 10, "0x%02X|0x%02X", generic, specific);
		if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
			nodeInfo->basicmapping = MapCommandClassBasic[buffer];
			return true;
		}
		else {
			// We didn't find a Generic+Specifc in the table, now we check
			// for Generic only
			snprintf(buffer, 10, "0x%02X", generic);

			// Check if we have a mapping in our map table
			if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
				nodeInfo->basicmapping = MapCommandClassBasic[buffer];
				return true;
			}
		}
	}
	return false;
}

void sigalrm_handler(int sig) {
	alarmset = false;
	Alarm currentAlarm = alarmlist.front();
	alarmlist.pop_front();
	if(atHome) {
		switch(s_mapStringTriggers[currentAlarm.description])
		{
			case Sunrise:
			{
				std::string dayScene;
				conf->GetDayScene(dayScene);
				try {
					std::cout << activateScene(dayScene);
				}
				catch (ProtocolException& e) {
					string what = "ProtocolException: ";
					what += e.what();
					std::cout << what << endl;
					std::cout << "trigger went off, but no Scene is set for Sunrise" << endl;
				}
				break;
			}
			case Sunset:
			{
				std::string nightScene;
				conf->GetNightScene(nightScene);
				try {
					std::cout << activateScene(nightScene);
				}
				catch (ProtocolException& e) {
					string what = "ProtocolException: ";
					what += e.what();
					std::cout << what << endl;
					std::cout << "trigger went off, but no Scene is set for Sunset" << endl;
				}
				break;
			}
			default:
				// check if the description can be used as a Scene name
				// if that fails, check if the description can be parsed as a command
			break;
		}
	}
	
	// more alarms on the alarmlist? Set the next one (the list is already sorted...)
	
	time_t now = time(NULL);
	
	while(!alarmlist.empty() && (alarmlist.front().alarmtime) <= now)
	{alarmlist.pop_front();}
					
	if(!alarmlist.empty() && !alarmset && (alarmlist.front().alarmtime > now)) {
		alarm((alarmlist.front().alarmtime - now));
		alarmset = true;
	}
}