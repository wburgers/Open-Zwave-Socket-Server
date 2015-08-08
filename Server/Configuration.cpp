#include "Configuration.h"
#include <string>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <map>

enum contents {Undefined = 0, tcp_port_n, ws_port_n, lat_n, lon_n, morningScene_n, dayScene_n, nightScene_n, awayScene_n, certificate_n, certificate_key_n, google_client_id_n, google_client_secret_n};
static std::map<std::string, contents> s_mapStringValues;

template <typename T>
T lexical_cast(const std::string& s) {
	std::stringstream ss(s);

	T result;
    if ((ss >> result).fail() || !(ss >> std::ws).eof()) {
		throw std::runtime_error("Bad cast");
	}

	return result;
}

Configuration::Configuration() :	conf_ini_location("./cpp/examples/server/Config.ini"), 
									ws_port(0),
									lat(0.0),
									lon(0.0),
									tcp_port(""),
									morningScene(""),
									dayScene(""),
									nightScene(""),
									awayScene(""),
									certificate(""),
									certificate_key(""),
									google_client_id(""),
									google_client_secret("") {
	create_string_map();
	std::ifstream conffile;
	if(open_filestream(conffile))
		if(parse_filestream(conffile))
		{}
}

bool Configuration::open_filestream(std::ifstream& conffile) {
	bool error = false;
	try {
		conffile.open(conf_ini_location.c_str());
		if(!conffile) {
			error = true;
			throw std::runtime_error("Could not open the file Config.ini");
		}
	}
	catch(std::exception const& e) {
		std::cout << "Exception: " << e.what() << std::endl;
	}
	return !error;
}

bool Configuration::parse_filestream(std::ifstream& conffile) {
	std::string input;
	int linenr = 0;
	bool error = false;
	try {
		while(getline(conffile,input)) {
			++linenr;
			std::size_t found = input.find('=');
			if (found!=std::string::npos) {
				std::string name = input.substr(0,found);
				std::string value = input.substr(found+1);
				if(parse_variable(name, value))
				{}
			}
			else {
				error = true;
				throw std::runtime_error("Error in the Config.ini file, unable to parse line" + linenr);
			}
		}
		conffile.close();
	}
	catch(std::exception const& e) {
		std::cout << "Exception: " << e.what() << std::endl;
	}
	return !error;
}

bool Configuration::parse_variable(std::string name, std::string value) {
	try {
		switch(s_mapStringValues[name])
		{
			case tcp_port_n:
				tcp_port = value;
				break;
			case ws_port_n:
				ws_port = lexical_cast<int>(value);
				break;
			case lat_n:
				lat = lexical_cast<float>(value);
				break;
			case lon_n:
				lon = lexical_cast<float>(value);
				break;
			case morningScene_n:
				morningScene = value;
				break;
			case dayScene_n:
				dayScene = value;
				break;
			case nightScene_n:
				nightScene = value;
				break;
			case awayScene_n:
				awayScene = value;
				break;
			case certificate_n:
				certificate = value;
				break;
			case certificate_key_n:
				certificate_key = value;
				break;
			case google_client_id_n:
				google_client_id = value;
				break;
			case google_client_secret_n:
				google_client_secret = value;
			default:
				return false;
				break;
		}
	}
	catch (std::exception const& e) {
		std::cout << "Exception: " << e.what() << std::endl;
	}
	return true;
}

void Configuration::create_string_map() {
	s_mapStringValues["tcp_port"] = tcp_port_n;
	s_mapStringValues["ws_port"] = ws_port_n;
	s_mapStringValues["lat"] = lat_n;
	s_mapStringValues["lon"] = lon_n;
	s_mapStringValues["morningScene"] = morningScene_n;
	s_mapStringValues["dayScene"] = dayScene_n;
	s_mapStringValues["nightScene"] = nightScene_n;
	s_mapStringValues["awayScene"] = awayScene_n;
	s_mapStringValues["certificate"] = certificate_n;
	s_mapStringValues["certificate_key"] = certificate_key_n;
	s_mapStringValues["google_client_id"] = google_client_id_n;
	s_mapStringValues["google_client_secret"] = google_client_secret_n;
}

bool Configuration::GetTCPPort(std::string &port_) {
	port_ = tcp_port;
	return true;
}

bool Configuration::GetWSPort(int &port_) {
	if(ws_port == 0) {
		return false;
	}
	port_ = ws_port;
	return true;
}

bool Configuration::GetLocation(float &lat_, float &lon_) {
	if(lat == 0.0 || lon == 0.0) {
		return false;
	}
	lat_ = lat;
	lon_ = lon;
	return true;
}

bool Configuration::GetMorningScene(std::string &morningScene_) {
	morningScene_ = morningScene;
	return true;
}

bool Configuration::GetDayScene(std::string &dayScene_) {
	dayScene_ = dayScene;
	return true;
}
bool Configuration::GetNightScene(std::string &nightScene_) {
	nightScene_ = nightScene;
	return true;
}
bool Configuration::GetAwayScene(std::string &awayScene_) {
	awayScene_ = awayScene;
	return true;
}
bool Configuration::GetCertificateInfo(std::string &certificate_, std::string &certificate_key_) {
	certificate_ = certificate;
	certificate_key_ = certificate_key;
	return true;
}
bool Configuration::GetGoogleClientIdAndSecret(std::string &client_id_, std::string &client_secret_) {
	client_id_ = google_client_id;
	client_secret_ = google_client_secret;
	return true;
}