#include "eps.h"


namespace horst {

EPS::EPS()
	:
	battery_level{70} {}

std::vector<std::unique_ptr<Action>>
EPS::transform_to(const EPS & /*target*/) const {

	std::vector<std::unique_ptr<Action>> ret;

	return ret;
}

} // horst
