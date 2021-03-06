#pragma once


#include <memory>
#include <vector>

#include "../action/action.h"
#include "adcs.h"
#include "computer.h"
#include "eps.h"
#include "payload.h"
#include "thm.h"


namespace horst {

/**
 * State of the satellite.
 * Events update this state.
 */
class State {
public:
	State();
	virtual ~State() = default;

	/**
	 * Create a copy of this state.
	 * Use this to create the new target state.
	 */
	State copy() const;

	/**
	 * Return a list of actions necessary to perform
	 * to reach the given target state of the satellite.
	 *
	 * This function is the most important one of the whole satellite.
	 * All the logic of when to do what is implemented here!!!!
	 */
	std::vector<std::unique_ptr<Action>>
	transform_to(const State &target);

	/**
	  * As soon as we reach an EPS battery level below that we will
	  * go into safemode
	  */
	uint16_t battery_treshold;

	/**
	 * State of the satellite's main computer.
	 *
	 * Used for changes required on the system like program starts,
	 * shell commands etc.
	 */
	Computer computer;

	/**
	 * Safemode state of the satellite
	 * 0, if no safemode is active
	 * 1, safemode because of high temperature
	 * 2, safemode because of low battery
	 * 3, safemode, because someone requested that
	 */
	uint8_t safemode;

	/**
	 * This will be set by an incoming safemode request only and will force
	 * us to re-enter safemode, even if we are already in safemode
	 */
	bool enforced_safemode;

	/* manual mode deactivates horst logic */
	bool manualmode;

	/* Maneuver mode for not touching ADCS mode */
	bool maneuvermode;

	/** The leop sequence we are in */
	enum class leop_seq {
		UNDEPLOYED,
		DEPLOYED,
		DONE
	} leop;

	/** State of the power supply */
	EPS eps;

	/** State of the thermal subsystem */
	THM thm;

	/** State of the payload subsystem */
	Payload pl;

	/** State of the ADCS subsystem */
	ADCS adcs;

	/** Interpret string as leop sequence */
	static State::leop_seq str2leop(char* name);

	/** Interpret string as boolean */
	static bool str2bool(char* name);
};

} // horst
