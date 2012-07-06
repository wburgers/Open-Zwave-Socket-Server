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
#include <unistd.h>
#include <pthread.h>
#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Group.h"
#include "Notification.h"
#include "ValueStore.h"
#include "Value.h"
#include "ValueBool.h"

#include "ServerSocket.h"
#include "SocketException.h"
#include <string>
#include <iostream>
#include <stdio.h>
#include <vector>
#include <stdlib.h>
#include <sstream>
#include <iostream>

using namespace OpenZWave;

static uint32 g_homeId = 0;
static bool g_initFailed = false;

typedef struct {
    uint32 m_homeId;
    uint8 m_nodeId;
    bool m_polled;
    list<ValueID> m_values;
} NodeInfo;

// Value-Defintions of the different String values

static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t initCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------

NodeInfo* GetNodeInfo(Notification const* _notification) {
    uint32 const homeId = _notification->GetHomeId();
    uint8 const nodeId = _notification->GetNodeId();
    for (list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
        NodeInfo* nodeInfo = *it;
        if ((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
            return nodeInfo;
        }
    }

    return NULL;
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------

void OnNotification(Notification const* _notification, void* _context) {
    // Must do this inside a critical section to avoid conflicts with the main thread
    pthread_mutex_lock(&g_criticalSection);

    switch (_notification->GetType()) {
        case Notification::Type_ValueAdded:
        {
            if (NodeInfo * nodeInfo = GetNodeInfo(_notification)) {
                // Add the new value to our list
                nodeInfo->m_values.push_back(_notification->GetValueID());
            }
            break;
        }

        case Notification::Type_ValueRemoved:
        {
            if (NodeInfo * nodeInfo = GetNodeInfo(_notification)) {
                // Remove the value from out list
                for (list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
                    if ((*it) == _notification->GetValueID()) {
                        nodeInfo->m_values.erase(it);
                        break;
                    }
                }
            }
            break;
        }

        case Notification::Type_ValueChanged:
        {
            if (NodeInfo * nodeInfo = GetNodeInfo(_notification)) {
                // One of the node values has changed
                // TBD... //GetValueID()
                //nodeInfo = nodeInfo;
		for (list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
                    if ((*it) == _notification->GetValueID()) {
                        nodeInfo->m_values.erase(it);
                        break;
                    }
                }
		nodeInfo->m_values.push_back(_notification->GetValueID());	
//Todo: clean up this update.  This was a fast way to update the status
            }
            break;
        }

        case Notification::Type_Group:
        {
            if (NodeInfo * nodeInfo = GetNodeInfo(_notification)) {
                // One of the node's association groups has changed
                // TBD...
                nodeInfo = nodeInfo;
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
            for (list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
                NodeInfo* nodeInfo = *it;
                if ((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
                    g_nodes.erase(it);
                    break;
                }
            }
            break;
        }

        case Notification::Type_NodeEvent:
        {
            if (NodeInfo * nodeInfo = GetNodeInfo(_notification)) {
                // We have received an event from the node, caused by a
                // basic_set or hail message.
                // TBD...               
                nodeInfo = nodeInfo;
            }
            break;
        }

        case Notification::Type_PollingDisabled:
        {
            if (NodeInfo * nodeInfo = GetNodeInfo(_notification)) {
                nodeInfo->m_polled = false;
            }
            break;
        }

        case Notification::Type_PollingEnabled:
        {
            if (NodeInfo * nodeInfo = GetNodeInfo(_notification)) {
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
        {
            pthread_cond_broadcast(&initCond);
            break;
        }

        default:
        {
        }
    }

    pthread_mutex_unlock(&g_criticalSection);
}

/******** DOSTUFF() *********************
 There is a separate instance of this function 
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/

void split(const string& s, char c, vector<string>& v) {
    string::size_type i = 0;
    string::size_type j = s.find(c);
    while (j != string::npos) {
        v.push_back(s.substr(i, j - i));
        i = ++j;
        j = s.find(c, j);
        if (j == string::npos)
            v.push_back(s.substr(i, s.length()));
    }
}

string trim(string s) {
    return s.erase(s.find_last_not_of(" \n\r\t") + 1);
}

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    pthread_mutexattr_t mutexattr;

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_criticalSection, &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);

    pthread_mutex_lock(&initMutex);

    // Create the OpenZWave Manager.
    // The first argument is the path to the config files (where the manufacturer_specific.xml file is located
    // The second argument is the path for saved Z-Wave network state and the log file.  If you leave it NULL 
    // the log file will appear in the program's working directory.
    Options::Create("../../../../config/", "", "");
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

    pthread_cond_wait(&initCond, &initMutex);

    if (!g_initFailed) {

        //Manager::Get()->WriteConfig(g_homeId);

        Driver::DriverData data;
        Manager::Get()->GetDriverStatistics(g_homeId, &data);

        printf("SOF: %d ACK Waiting: %d Read Aborts: %d Bad Checksums: %d\n", data.s_SOFCnt, data.s_ACKWaiting, data.s_readAborts, data.s_badChecksum);
        printf("Reads: %d Writes: %d CAN: %d NAK: %d ACK: %d Out of Frame: %d\n", data.s_readCnt, data.s_writeCnt, data.s_CANCnt, data.s_NAKCnt, data.s_ACKCnt, data.s_OOFCnt);
        printf("Dropped: %d Retries: %d\n", data.s_dropped, data.s_retries);

        printf("***************************************************** \n");
        printf("6004 ZWaveCommander Server \n");

        //Manager::Get()->SetNodeName(g_homeId, 3, "Lampshade");
        
        
        try {
            // Create the socket
            ServerSocket server(6004);
            while (true) {
                //pthread_mutex_lock(&g_criticalSection);
                // Do stuff            
                ServerSocket new_sock;
                server.accept(new_sock);
                try {
                    while (true) {


                        std::string data;
                        new_sock >> data;

                        //get zwave commands

                        //if (trim(data.c_str()) == "BYE") exit(0);

                        //give list of devices
                        if (trim(data.c_str()) == "ALIST") {
                            string device;
                            for (list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
                                NodeInfo* nodeInfo = *it;
                                int nodeID = nodeInfo->m_nodeId;
                                //This refreshed node could be cleaned up - I added the quick hack so I would get status
                                // or state changes that happened on the switch itself or due to another event / [rpcess
				                bool isRePolled= Manager::Get()->RefreshNodeInfo(g_homeId, nodeInfo->m_nodeId);
                                string nodeType = Manager::Get()->GetNodeType(g_homeId, nodeInfo->m_nodeId);
                                string nodeName = Manager::Get()->GetNodeName(g_homeId, nodeInfo->m_nodeId);
                                string nodeZone = Manager::Get()->GetNodeLocation(g_homeId, nodeInfo->m_nodeId);



				string nodeValue ="";//(string) Manager::Get()->RequestNodeState(g_homeId, nodeInfo->m_nodeId);
//The point of this was to help me figure out what the node values looked like
 for (list<ValueID>::iterator it5 = nodeInfo->m_values.begin(); it5 != nodeInfo->m_values.end(); ++it5) {
			string tempstr="";
			Manager::Get()->GetValueAsString(*it5,&tempstr);                   
			tempstr= "="+tempstr; 
                        nodeValue+=" "+ Manager::Get()->GetValueLabel(*it5) +tempstr;
                        
                    
                }


                                if (nodeName.size() == 0) nodeName = "Undefined";

                                if (nodeType != "Static PC Controller") {
                                    stringstream ssNodeName, ssNodeId, ssNodeType, ssNodeZone, ssNodeValue;
                                    ssNodeName << nodeName;
                                    ssNodeId << nodeID;
                                    ssNodeType << nodeType;
                                    ssNodeZone << nodeZone;
				    ssNodeValue << nodeValue;
                                    device += "DEVICE~" + ssNodeName.str() + "~" + ssNodeId.str() + "~"+ ssNodeZone.str() +"~" + ssNodeType.str() + "~" + ssNodeValue.str() + "#";
                                }
                            }
                            device = device.substr(0, device.size() - 1) + "\n";                           
                            printf("Sent Device List \n");
                            new_sock << device;
                        }

                        vector<string> v;
                        split(data, '~', v);

                        string command, deviceType;

                        if (v.size() > 0) {
                            //check Type of Command
                            stringstream sCommand;
                            sCommand << v[0].c_str();
                            string command = sCommand.str();
                            
                            printf("Command: %s", command.c_str());
                            if (command == "DEVICE") {
                                //check type
                                deviceType = v[v.size() - 1];

                                int Node = 0;
                                int Level = 0;
                                string Type = "";

                                Level = atoi(v[2].c_str());
                                Node = atoi(v[1].c_str());
                                Type = v[3].c_str();
                                Type = trim(Type);

                                if ((Type == "Multilevel Switch") || (Type == "Multilevel Power Switch")) {
                                    pthread_mutex_lock(&g_criticalSection);
                                    Manager::Get()->SetNodeLevel(g_homeId, Node, Level);
                                    pthread_mutex_unlock(&g_criticalSection);
                                }

                                if (Type == "Binary Switch") {
                                    pthread_mutex_lock(&g_criticalSection);
                                    if (Level == 0) {
                                        Manager::Get()->SetNodeOff(g_homeId, Node);

                                    } else {
                                        Manager::Get()->SetNodeOn(g_homeId, Node);
                                    }
                                    pthread_mutex_unlock(&g_criticalSection);
                                }

                                stringstream ssNode, ssLevel;
                                ssNode << Node;
                                ssLevel << Level;
                                string result = "MSG~ZWave Node=" + ssNode.str() + " Level=" + ssLevel.str() + "\n";
                                new_sock << result;
                            }

                            if (command == "SETNODE") {
                                int Node = 0;
                                string NodeName = "";
                                string NodeZone = "";
                                
                                Node = atoi(v[1].c_str());
                                NodeName = v[2].c_str();
                                NodeName = trim(NodeName);
                                NodeZone = v[3].c_str();
                                
                                pthread_mutex_lock(&g_criticalSection);
                                Manager::Get()->SetNodeName(g_homeId, Node, NodeName);
                                Manager::Get()->SetNodeLocation(g_homeId, Node, NodeZone);
                                pthread_mutex_unlock(&g_criticalSection);
                                
                                stringstream ssNode, ssName, ssZone;
                                ssNode << Node;
                                ssName << NodeName;
                                ssZone << NodeZone;
                                string result = "MSG~ZWave Name set Node=" + ssNode.str() + " Name=" + ssName.str() + " Zone=" + ssZone.str() + "\n";
                                new_sock << result;
                                
                                //save details to XML
                                Manager::Get()->WriteConfig(g_homeId);
                            }

                            //  sleep(5);
                        }


                    }
                } catch (SocketException&) {
                }
                //pthread_mutex_unlock(&g_criticalSection);
                //sleep(5);
            }
        } catch (SocketException& e) {
            printf("Exception was caught:");
        }
    }

    Manager::Destroy();

    pthread_mutex_destroy(&g_criticalSection);
    return 0;
}
