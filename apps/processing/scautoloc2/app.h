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




#ifndef SEISCOMP_APPLICATIONS_AUTOLOC_APP_H
#define SEISCOMP_APPLICATIONS_AUTOLOC_APP_H

#include <queue>
#include <seiscomp/datamodel/pick.h>
#include <seiscomp/datamodel/amplitude.h>
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/datamodel/eventparameters.h>
#include <seiscomp/datamodel/inventory.h>
#include <seiscomp/client/application.h>

#include <seiscomp/autoloc/autoloc.h>
#include <seiscomp/autoloc/objectqueue.h>


namespace Seiscomp {

namespace Applications {


// PickAmplitudeSet
//
// A single set of pick/amplitude objects forming the input data of Autoloc.

class PickAmplitudeSet {
	public:
		PickAmplitudeSet(
			const Seiscomp::DataModel::Pick *_pick,
			const Seiscomp::DataModel::Amplitude *_amplitudeSNR,
			const Seiscomp::DataModel::Amplitude *_amplitudeAbs)
		{
			pick = _pick;
			amplitudeSNR = _amplitudeSNR;
			amplitudeAbs = _amplitudeAbs;
		}

		Seiscomp::DataModel::PickCPtr pick;
		Seiscomp::DataModel::AmplitudeCPtr amplitudeSNR;
		Seiscomp::DataModel::AmplitudeCPtr amplitudeAbs;
};

/*
class PickAmplitudeBuffer {
	PickAmplitudeBuffer(
		Seiscomp::DataModel::DatabaseArchive *ar,
		size_t bufferSize);
};
*/

class AutolocApp :
	public Client::Application,
	// Derived from Autoloc3 mainly because we re-implement here
	// the _report() method to allow both XML and messaging output.
        protected Autoloc::Autoloc3
{
	public:
		AutolocApp(int argc, char **argv);
		~AutolocApp();

	private:
		// Startup
		void createCommandLineDescription();
		bool validateParameters();
		bool initConfiguration();
		bool initInventory();

		bool init();
		bool run();

	private:
		// Read past events from database.
		void readPastEvents();

		// Read picks and corresponding amplitudes from database.
		void readPicksForOrigin(const Seiscomp::DataModel::Origin*);

	private:
		// Playback
		bool runFromXMLFile(const char *fname);
		bool runFromEPFile(const char *fname);

	private:
		// Processing
		bool feed(DataModel::Pick*);
		bool feed(DataModel::Amplitude*);
		bool feed(DataModel::Origin*);

		// This is the main SC object source
		void addObject(
			const std::string& parentID,
			DataModel::Object*);

		// // These may be needed in future:
		// void updateObject(
		// 	const std::string& parentID,
		// 	DataModel::Object*);
		// void removeObject(
		// 	const std::string& parentID,
		// 	DataModel::Object*);

		// re-implemented to support XML and messaging output
		virtual bool _report(DataModel::Origin*);

		void handleTimeout();

	public:
		virtual DataModel::Pick* loadPick(
			const std::string &pickID);

		virtual DataModel::Amplitude* loadAmplitude(
			const std::string &pickID,
			const std::string &amplitudeType);

		size_t loadPicksAndAmplitudesForOrigin(
			const Seiscomp::DataModel::Origin *scorigin,
			std::vector<PickAmplitudeSet> &pa);

	private:
		// shutdown
		void done();
		void handleAutoShutdown();

	private:
		std::string _inputFileXML; // for XML playback
		std::string _inputEPFile; // for offline processing
		std::string _stationLocationFile;

		// object queue used for XML playback
		DataModel::PublicObjectQueue objectQueue;

		double _playbackSpeed;
		Core::Time playbackStartTime;
		Core::Time objectsStartTime;
		Core::Time syncTime;
		unsigned int objectCount;

		DataModel::EventParametersPtr ep;
		DataModel::InventoryPtr inventory;

		Autoloc::Config _config;
		int _keepEventsTimeSpan;
		int _wakeUpTimout;

		ObjectLog *_inputPicks;
		ObjectLog *_inputAmps;
		ObjectLog *_inputOrgs;
		ObjectLog *_outputOrgs;
};


}  // namespace Applications

}  // namespace Seiscomp

#endif
