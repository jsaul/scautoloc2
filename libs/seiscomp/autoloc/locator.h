/***************************************************************************
 * Copyright (C) GFZ Potsdam                                               *
 * All rights reserved.                                                    *
 *                                                                         *
 * GNU Affero General Public License Usage                                 *
 * This file may be used under the terms of the GNU Affero                 *
 * Public License version 3.0 as published by the Free Software Foundation *
 * and appearing in the file LICENSE included in the packaging of this     *
 * file. Please review the following information to ensure the GNU Affero  *
 * Public License version 3.0 requirements will be met:                    *
 * https://www.gnu.org/licenses/agpl-3.0.html.                             *
 ***************************************************************************/


#ifndef SEISCOMP_LIBAUTOLOC_LOCATOR_H_INCLUDED
#define SEISCOMP_LIBAUTOLOC_LOCATOR_H_INCLUDED

#include <string>
#include <map>

#include <seiscomp/seismology/locator/locsat.h>
#include <seiscomp/autoloc/datamodel.h>
#include <seiscomp/config/config.h>


namespace Seiscomp {

namespace Autoloc {

DEFINE_SMARTPOINTER(MySensorLocationDelegate);

class MySensorLocationDelegate : public Seiscomp::Seismology::SensorLocationDelegate
{
	public:
		~MySensorLocationDelegate();
	public:
		virtual Seiscomp::DataModel::SensorLocation*
			getSensorLocation(Seiscomp::DataModel::Pick *pick) const;
		void setStation(const Autoloc::DataModel::Station *station);
	private:
		typedef std::map<std::string, Seiscomp::DataModel::SensorLocationPtr> SensorLocationList;
		SensorLocationList _sensorLocations;
};



class Locator {
	public:
		Locator();
		~Locator();

                void setConfig(const Seiscomp::Config::Config*);
		void setProfile(const std::string &name);

		bool init();

		void setStation(const Autoloc::DataModel::Station *station);

		void setMinimumDepth(double);
		void setFixedDepth(double depth, bool use=true);
		void useFixedDepth(bool use=true);

	public:
		Autoloc::DataModel::Origin *relocate(
			const Autoloc::DataModel::Origin *origin);


	private:
		// this is the SeisComP-level relocate
		Autoloc::DataModel::Origin *_screlocate(
			const Autoloc::DataModel::Origin *origin);

	private:
		Seiscomp::Seismology::LocatorInterfacePtr sclocator;
		const Seiscomp::Config::Config *scconfig;

		MySensorLocationDelegatePtr sensorLocationDelegate;

		double _minDepth;

		// for debugging count locator calls
		size_t _locatorCallCounter;
};

}  // namespace Autoloc

}  // namespace Seiscomp

#endif
