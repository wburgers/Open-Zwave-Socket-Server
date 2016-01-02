#ifndef OZWSS_CONFIGURATION_H_
#define OZWSS_CONFIGURATION_H_

#include <string>
namespace OZWSS {
	class Configuration {
		private:
			std::string conf_ini_location;
			float lat, lon;
			int ws_port;
			std::string tcp_port, morningScene, dayScene, nightScene, awayScene, certificate, certificate_key, google_client_id, google_client_secret;
			bool open_filestream(std::ifstream& conffile);
			bool parse_filestream(std::ifstream& conffile);
			bool parse_variable(std::string name, std::string value);
			void create_string_map();
		public:
			Configuration(std::string conf_ini_location_);
			bool GetTCPPort(std::string &port_);
			bool GetWSPort(int &port_);
			bool GetLocation(float &lat_, float &lon_);
			bool GetMorningScene(std::string &morningScene_);
			bool GetDayScene(std::string &dayScene_);
			bool GetNightScene(std::string &nightScene_);
			bool GetAwayScene(std::string &awayScene_);
			bool GetCertificateInfo(std::string &certificate_, std::string &certificate_key_);
			bool GetGoogleClientIdAndSecret(std::string &client_id_, std::string &client_secret_);
	};
}
#endif // OZWSS_CONFIGURATION_H_
