#include "adcs.h"

#include <s3tp/core/Logger.h>

#include "../util.h"


namespace horst {

ADCS::ADCS()
	:
	pointing{adcs_state::NONE},
	requested{adcs_state::NONE} {}

std::vector<std::unique_ptr<Action>>
ADCS::transform_to(const ADCS & /*target*/) const {
	std::vector<std::unique_ptr<Action>> ret;
	return ret;
}

ADCS::adcs_state ADCS::str2state(char* name) {

	// Convert to upper case
	char *p = name;
	while (*p) {
		*p = toupper(*p);
		p++;
	}

	switch(util::str2int(name)) {
	case util::str2int("NONE"):
		return ADCS::adcs_state::NONE;
	case util::str2int("SLEEP"):
		return ADCS::adcs_state::SLEEP;
	case util::str2int("ATTDET"):
		return ADCS::adcs_state::ATTDET;
	case util::str2int("DETUMB"):
		return ADCS::adcs_state::DETUMB;
	case util::str2int("NADIR"):
		return ADCS::adcs_state::NADIR;
	case util::str2int("SUN"):
		return ADCS::adcs_state::SUN;
	case util::str2int("FLASH"):
		return ADCS::adcs_state::FLASH;
	case util::str2int("EXP"):
		return ADCS::adcs_state::EXP;
	case util::str2int("TEST"):
		return ADCS::adcs_state::TEST;
	case util::str2int("POWEROFF"):
		return ADCS::adcs_state::POWEROFF;
	default:
		LOG_WARN("Could not interpret '" + std::string(name) + "' as ADCS state!");
		return ADCS::adcs_state::NONE;
	}
}

} // horst
