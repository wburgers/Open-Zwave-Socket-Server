#include <string>
#include <iostream>
#include <fstream>
#include <stdexcept>

class Configuration {
	private:
		float lat, lon;
		std::string conf_ini_location = "./cpp/examples/linux/server/Config.ini";
		bool open_filestream(std::ifstream& conffile);
		bool parse_filestream(std::ifstream& conffile);
		bool parse_variable(std::string name, std::string value);
		void create_string_map();
	public:
		Configuration();
		~Configuration();
		bool GetLocation(float &lat_, float &lon_);
};