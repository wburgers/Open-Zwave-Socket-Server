#include <string>
#include <iostream>
#include <fstream>
#include <stdexcept>

class Configuration {
	private:
		std::string conf_ini_location;
		float lat, lon;
		std::string dayScene, nightScene, awayScene;
		bool open_filestream(std::ifstream& conffile);
		bool parse_filestream(std::ifstream& conffile);
		bool parse_variable(std::string name, std::string value);
		void create_string_map();
	public:
		Configuration();
		bool GetLocation(float &lat_, float &lon_);
		bool GetDayScene(std::string &dayScene_);
		bool GetNightScene(std::string &nightScene_);
		bool GetAwayScene(std::string &awayScene_);
};