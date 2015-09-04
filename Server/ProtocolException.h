#ifndef OZWSS_PROTOCOLEXCEPTION_H_
#define OZWSS_PROTOCOLEXCEPTION_H_

#include <stdexcept>
#include <string>
#include <sstream>

namespace OZWSS {
	class ProtocolException : public std::runtime_error {
		public:
			ProtocolException (std::string s, int code) : std::runtime_error(s), m_code(code) {};
			virtual ~ProtocolException() throw () {}

			std::string what() {
				std::stringstream code;
				code << m_code;
				std::string what = "";
				what += code.str();
				what += ": ";
				what += std::runtime_error::what();
				return what;
			}

		private:
			int m_code;
	};
}

#endif // OZWSS_PROTOCOLEXCEPTION_H_