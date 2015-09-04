#ifndef OZWSS_SUNRISE_H_
#define OZWSS_SUNRISE_H_

#include <time.h>

namespace OZWSS {
	bool GetSunriseSunset(time_t &tSunrise,time_t &tSunset,float latitude,float longitude);
}
#endif // OZWSS_SUNRISE_H_
