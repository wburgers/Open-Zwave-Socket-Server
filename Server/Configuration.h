#include <string>
#include <iostream>
#include <fstream>
#include <stdexcept>

class Configuration {
	private:
		std::string conf_ini_location;
		int tcp_port, ws_port;
		float lat, lon;
		std::string morningScene, dayScene, nightScene, awayScene, certificate, certificate_key, google_client_id, google_client_secret;
		bool open_filestream(std::ifstream& conffile);
		bool parse_filestream(std::ifstream& conffile);
		bool parse_variable(std::string name, std::string value);
		void create_string_map();
	public:
		Configuration();
		bool GetTCPPort(int &port_);
		bool GetWSPort(int &port_);
		bool GetLocation(float &lat_, float &lon_);
		bool GetMorningScene(std::string &morningScene_);
		bool GetDayScene(std::string &dayScene_);
		bool GetNightScene(std::string &nightScene_);
		bool GetAwayScene(std::string &awayScene_);
		bool GetCertificateInfo(std::string &certificate_, std::string &certificate_key_);
		bool GetGoogleClientIdAndSecret(std::string &client_id_, std::string &client_secret_);
};