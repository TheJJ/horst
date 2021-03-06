#pragma once

#include <memory>
#include <vector>

#include "../action/action.h"
#include "state_component.h"


namespace horst {


/**
 * Represents the thermal status
 */
class THM : public StateComponent {
public:
	THM();

	std::vector<std::unique_ptr<Action>> transform_to(const THM &target) const;

	/**
	 * Overall temperature state of the satellite.
	 */
	enum class overall_temp {
		OK,
		WARN,
		ALARM,
	} all_temp;

	static THM::overall_temp str2temp(const char* name);
};

} // horst
