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




#define SEISCOMP_COMPONENT Autoloc
#include <seiscomp/logging/log.h>
#include <seiscomp/client/inventory.h>
#include <seiscomp/datamodel/eventparameters.h>
#include <seiscomp/datamodel/journaling_package.h>
#include <seiscomp/datamodel/utils.h>
#include <seiscomp/utils/files.h>
#include <seiscomp/core/datamessage.h>
#include <seiscomp/io/archive/xmlarchive.h>

#include "app.h"
#include <seiscomp/autoloc/datamodel.h>
#include <seiscomp/autoloc/sc3adapters.h>
#include <seiscomp/autoloc/util.h>
#include <seiscomp/autoloc/stationlocationfile.h>


namespace Seiscomp {

namespace Applications {


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
AutolocApp::AutolocApp(int argc, char **argv)
	: Application(argc, argv), Autoloc::Autoloc3()
	, objectCount(0)
	, _inputPicks(nullptr)
	, _inputAmps(nullptr)
	, _inputOrgs(nullptr)
	, _outputOrgs(nullptr)
{
	setMessagingEnabled(true);

	setPrimaryMessagingGroup("LOCATION");

	addMessagingSubscription("PICK");
	addMessagingSubscription("AMPLITUDE");
	addMessagingSubscription("LOCATION");

	_keepEventsTimeSpan = 86400; // one day
	_wakeUpTimout = 5; // wake up every 5 seconds to check pending operations

	_playbackSpeed = 1;
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
AutolocApp::~AutolocApp() {}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void AutolocApp::createCommandLineDescription() {
	Client::Application::createCommandLineDescription();

	commandline().addGroup("Mode");
	commandline().addOption(
		"Mode","test", "Do not send any object");
	commandline().addOption(
		"Mode", "offline",
		"Do not connect to a messaging server. "
		"Instead a station-locations.conf file can be provided. "
		"This implies --test and --playback");
	commandline().addOption(
		"Mode", "playback", "Flush origins immediately without delay");
	commandline().addOption(
		"Mode", "xml-playback", "TODO"); // TODO

	commandline().addGroup("Input");
	commandline().addOption(
		"Input", "input,i",
		"XML input file for --xml-playback",
		&_inputFileXML, false);
	commandline().addOption(
		"Input", "ep",
		"Event parameters XML file for offline processing of all "
		"contained picks and amplitudes",
		&_inputFileEP, false);

	commandline().addGroup("Settings");
	commandline().addOption(
		"Settings", "station-locations",
		"The station-locations.conf file to use when in offline mode. "
		"If no file is given the database is used.",
		&_stationLocationFile, false);
	commandline().addOption(
		"Settings", "station-config",
		"The station configuration file",
		&_config.stationConfig, false);
	commandline().addOption(
		"Settings", "grid",
		"The grid configuration file",
		&_config.gridConfigFile, false);
	commandline().addOption(
		"Settings", "pick-log",
		"The pick log file. "
		"Providing a file name enables logging picks even when "
		"disabled by configuration.",
	        &_config.pickLogFile, false);

	commandline().addOption(
		"Settings", "default-depth",
		"Default depth for locating", &_config.defaultDepth);
	commandline().addOption(
		"Settings", "default-depth-stickiness",
		"", &_config.defaultDepthStickiness);
	commandline().addOption(
		"Settings", "max-sgap",
		"Maximum secondary azimuthal gap", &_config.maxAziGapSecondary);
	commandline().addOption(
		"Settings", "max-rms",
		"Maximum RMS residual to be considered",
		&_config.maxRMS);
	commandline().addOption(
		"Settings", "max-residual",
		"Maximum travel-time residual per station to be considered",
		&_config.maxResidualUse);
	commandline().addOption(
		"Settings", "max-station-distance",
		"Maximum distance of stations to be used",
		&_config.maxStaDist);
	commandline().addOption(
		"Settings", "max-nucleation-distance-default",
		"Default maximum distance of stations to be used for "
		"nucleating new origins",
		&_config.defaultMaxNucDist);
	commandline().addOption(
		"Settings", "min-pick-affinity", "", &_config.minPickAffinity);
	commandline().addOption(
		"Settings", "min-phase-count",
		"Minimum number of picks for an origin to be reported",
		&_config.minPhaseCount);
	commandline().addOption(
		"Settings", "min-score",
		"Minimum score for an origin to be reported",
		&_config.minScore);
	commandline().addOption(
		"Settings", "min-pick-snr",
		"Minimum SNR for a pick to be processed",
		&_config.minPickSNR);
	commandline().addOption(
		"Settings", "xxl-enable", "", &_config.xxlEnabled);
	commandline().addOption(
		"Settings", "xxl-min-phase-count",
		"Minimum number of picks for an XXL origin to be reported",
		&_config.xxlMinPhaseCount);
	commandline().addOption(
		"Settings", "xxl-min-amplitude",
		"Flag pick as XXL if BOTH snr and amplitude exceed threshold",
		&_config.xxlMinAmplitude);
	commandline().addOption(
		"Settings", "xxl-min-snr",
		"Flag pick as XXL if BOTH snr and amplitude exceed threshold",
		&_config.xxlMinSNR);
	commandline().addOption(
		"Settings", "xxl-max-distance", "", &_config.xxlMaxStaDist);
	commandline().addOption(
		"Settings", "xxl-max-depth", "", &_config.xxlMaxDepth);
	commandline().addOption(
		"Settings", "xxl-dead-time", "", &_config.xxlDeadTime);

//	commandline().addOption(
//		"Settings", "min-sta-count-ignore-pkp", 
//		"Minimum station count for which we ignore PKP phases",
//		&_config.minStaCountIgnorePKP);
	commandline().addOption(
		"Settings", "min-score-bypass-nucleator",
		"Minimum score at which the nucleator is bypassed",
		&_config.minScoreBypassNucleator);

	commandline().addOption(
		"Settings", "keep-events-timespan",
		"The timespan to keep events", &_keepEventsTimeSpan);

	commandline().addOption(
		"Settings", "cleanup-interval",
		"The object cleanup interval in seconds",
		&_config.cleanupInterval);
	commandline().addOption(
		"Settings", "max-age",
		"During cleanup all objects older than maxAge (in seconds) "
		"are removed (maxAge == 0 => disable cleanup)",
		&_config.maxAge);
	commandline().addOption(
		"Settings", "wakeup-interval",
		"The interval in seconds to check pending operations",
		&_wakeUpTimout);
	commandline().addOption(
		"Settings", "speed",
		"Set this to greater 1 to increase XML playback speed",
		&_playbackSpeed);
	commandline().addOption(
		"Settings", "dynamic-pick-threshold-interval",
		"The interval in seconds in which to check for extraordinarily"
		"high pick activity, resulting in a dynamically increased pick"
		"threshold",
		&_config.dynamicPickThresholdInterval);

	commandline().addOption(
		"Settings", "use-manual-picks",
		"allow use of manual picks of our own agency for nucleation "
		"and location");
	commandline().addOption(
		"Settings", "use-manual-origins",
		"allow use of manual origins from our own agency");
	commandline().addOption(
		"Settings", "use-imported-origins",
		"allow use of imported origins from trusted agencies as "
		"configured in 'processing.whitelist.agencies");
	commandline().addOption(
		"Settings", "adopt-imported-origin-depth",
		"adopt the depths of imported origins");
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::validateParameters()
{
	if ( commandline().hasOption("offline") ) {
		_config.offline = true;
		_config.playback = true;
		_config.test = true;
	}
	else
		_config.offline = false;

	if ( !_inputFileEP.empty() ) {
		_config.playback = true;
		_config.offline = true;
	}


	if ( _config.offline ) {
		setMessagingEnabled(false);
		if ( !_stationLocationFile.empty() )
			setDatabaseEnabled(false, false);
	}

	// If a station location file was specified.
	if ( _stationLocationFile.size() ) {
		// inventory will be read from station location file
		setLoadStationsEnabled(false);
		setDatabaseEnabled(false, false);
	}
	else {
		// inventory will be loaded from database
		setLoadStationsEnabled(true);

		if ( !isInventoryDatabaseEnabled() )
			setDatabaseEnabled(false, false);
		else
			setDatabaseEnabled(true, true);
	}

	// Maybe we do want to allow sending of origins in offline mode?
	if ( commandline().hasOption("test") )
		_config.test = true;

	if ( commandline().hasOption("playback") )
		_config.playback = true;

	if ( commandline().hasOption("use-manual-picks") )
		_config.useManualPicks = true;

	if ( commandline().hasOption("use-manual-origins") )
		_config.useManualOrigins = true;

	if ( commandline().hasOption("use-imported-origins") )
		_config.useImportedOrigins = true;

	if ( commandline().hasOption("adopt-imported-origin-depth") )
		_config.adoptImportedOriginDepth = true;

	if ( commandline().hasOption("try-default-depth") )
		_config.tryDefaultDepth = true;

	if ( commandline().hasOption("adopt-manual-depth") )
		_config.adoptManualDepth = true;

	_config.maxResidualKeep = 3*_config.maxResidualUse;

	if ( ! _config.pickLogFile.empty() ) {
		_config.pickLogEnable = true;
	}

	return Client::Application::validateParameters();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::initConfiguration()
{
	if ( !Client::Application::initConfiguration() )
		return false;

	// support deprecated configuration, deprecated since 2020-11-16
	try {
		_config.maxAge = configGetDouble("autoloc.maxAge");
		SEISCOMP_ERROR(
			"autoloc.maxAge is deprecated."
		        " Use buffer.pickKeep instead!");
	}
	catch (...) {}

	// override deprecated configuration if value is set
	try {
		_config.maxAge = configGetDouble("buffer.pickKeep");
	}
	catch (...) {}

	try {
		_config.pickAuthors = configGetStrings("autoloc.pickAuthors");
	}
	catch (...) {}


	// support deprecated configuration, deprecated since 2020-11-16
	try {
		_keepEventsTimeSpan = configGetInt("keepEventsTimeSpan");
		SEISCOMP_ERROR(
			"keepEventsTimeSpan is deprecated. "
			"Use buffer.originKeep instead!");
	}
	catch ( ... ) {}

	// override deprecated configuration if value is set
	try {
		_keepEventsTimeSpan = configGetInt("buffer.originKeep");
	}
	catch ( ... ) {}

	// support deprecated configuration, deprecated since 2020-11-16
	try {
		_config.cleanupInterval =
			configGetDouble("autoloc.cleanupInterval");
		SEISCOMP_ERROR(
			"Configuration parameter autoloc.cleanupInterval "
			"is deprecated. Use buffer.cleanupInterval instead!");
	}
	catch (...) {}

	try {
		_config.cleanupInterval =
			configGetDouble("buffer.cleanupInterval");
	}
	catch (...) {}

	try {
		_config.defaultDepth =
			configGetDouble("locator.defaultDepth");
	}
	catch (...) {}

	try {
		_config.defaultDepthStickiness =
			configGetDouble("autoloc.defaultDepthStickiness");
	}
	catch (...) {}

	try {
		_config.tryDefaultDepth =
			configGetBool("autoloc.tryDefaultDepth");
	}
	catch (...) {}

	try {
		_config.adoptManualDepth =
			configGetBool("autoloc.adoptManualDepth");
	}
	catch (...) {}

	try {
		_config.minimumDepth =
			configGetDouble("locator.minimumDepth");
	}
	catch (...) {}

	try {
		_config.maxAziGapSecondary =
			configGetDouble("autoloc.maxSGAP");
	}
	catch (...) {}

	try {
		_config.maxRMS =
			configGetDouble("autoloc.maxRMS");
	}
	catch (...) {}

	try {
		_config.maxDepth =
			configGetDouble("autoloc.maxDepth");
	}
	catch (...) {}

	try {
		_config.maxResidualUse =
			configGetDouble("autoloc.maxResidual");
	}
	catch (...) {}

	try {
		_config.maxStaDist =
			configGetDouble("autoloc.maxStationDistance"); }
	catch (...) {}

	try {
		_config.defaultMaxNucDist =
			configGetDouble("autoloc.defaultMaxNucleationDistance");
	}
	catch (...) {}

	try {
		_config.xxlEnabled =
			configGetBool("autoloc.xxl.enable");
	}
	catch (...) {}

	try {
		_config.xxlMinAmplitude =
			configGetDouble("autoloc.xxl.minAmplitude");
	}
	catch (...) {
		try {
			// deprecated since 2013-06-26
			_config.xxlMinAmplitude =
				configGetDouble("autoloc.thresholdXXL");
			SEISCOMP_ERROR(
				"autoloc.thresholdXXL is deprecated. "
				"Use autoloc.xxl.minAmplitude instead!");
		}
		catch (...) {}
	}

	try {
		_config.xxlMaxStaDist =
			configGetDouble("autoloc.xxl.maxStationDistance");
	}
	catch (...) {
		try {
			// deprecated since 2013-06-26
			_config.xxlMaxStaDist =
				configGetDouble("autoloc.maxStationDistanceXXL");
			SEISCOMP_ERROR(
				"autoloc.maxStationDistanceXXL is deprecated. "
				"Use autoloc.xxl.maxStationDistance instead!");
		}
		catch (...) {}
	}

	try {
		_config.xxlMinPhaseCount =
			configGetInt("autoloc.xxl.minPhaseCount"); }
	catch (...) {
		try {
			// deprecated since 2013-06-26
			_config.xxlMinPhaseCount =
				configGetInt("autoloc.minPhaseCountXXL");
			SEISCOMP_ERROR(
				"autoloc.minPhaseCountXXL is deprecated. "
				"Use autoloc.xxl.minPhaseCount instead!");
		}
		catch (...) {}
	}

	try {
		_config.xxlMinSNR =
			configGetDouble("autoloc.xxl.minSNR");
	}
	catch (...) {}

	try {
		_config.xxlMaxDepth =
			configGetDouble("autoloc.xxl.maxDepth");
	}
	catch (...) {}

	try {
		_config.xxlDeadTime =
			configGetDouble("autoloc.xxl.deadTime");
	}
	catch (...) {}

	try {
		_config.minPickSNR = configGetDouble("autoloc.minPickSNR");
	}
	catch (...) {}

	try {
		_config.minPickAffinity =
			configGetDouble("autoloc.minPickAffinity"); }
	catch (...) {}

	try {
		_config.minPhaseCount =
			configGetInt("autoloc.minPhaseCount");
	}
	catch (...) {}

	try {
		_config.minScore =
			configGetDouble("autoloc.minScore");
	}
	catch (...) {}

	try {
		_config.minScoreBypassNucleator =
			configGetDouble("autoloc.minScoreBypassNucleator");
	}
	catch (...) {}

//	try {
//		_config.minStaCountIgnorePKP =
//			configGetInt("autoloc.minStaCountIgnorePKP");
//	}
//	catch (...) {}

	try {
		_config.reportAllPhases =
			configGetBool("autoloc.reportAllPhases");
	}
	catch (...) {}

	try {
		_config.useManualPicks =
			configGetBool("autoloc.useManualPicks");
	}
	catch (...) {}

	try {
		_config.useManualOrigins =
			configGetBool("autoloc.useManualOrigins");
	}
	catch (...) {}

	try {
		_config.useImportedOrigins =
			configGetBool("autoloc.useImportedOrigins");
	}
	catch (...) {}

	try {
		_config.adoptImportedOriginDepth =
			configGetBool("autoloc.adoptImportedOriginDepth");
	}
	catch (...) {}

	try {
		_wakeUpTimout =
			configGetInt("autoloc.wakeupInterval");
	}
	catch (...) {}

	try {
		_config.publicationIntervalTimeSlope =
			configGetDouble(
				"autoloc.publicationIntervalTimeSlope");
	}
	catch ( ... ) {}

	try {
		_config.publicationIntervalTimeIntercept =
			configGetDouble(
				"autoloc.publicationIntervalTimeIntercept");
	}
	catch ( ... ) {}

	try {
		_config.publicationIntervalPickCount =
			configGetInt("autoloc.publicationIntervalPickCount");
	}
	catch ( ... ) {}

	try {
		_config.dynamicPickThresholdInterval =
			configGetDouble("autoloc.dynamicPickThresholdInterval");
	}
	catch ( ... ) {}

	try {
		_config.gridConfigFile =
			Environment::Instance()->absolutePath(
				configGetString("autoloc.grid"));
	}
	catch (...) {
		_config.gridConfigFile =
			Environment::Instance()->shareDir() +
			"/scautoloc/grid.conf";
	}

	try {
		_config.stationConfig =
			Environment::Instance()->absolutePath(configGetString(
				"autoloc.stationConfig"));
	}
	catch (...) {
		_config.stationConfig =
			Environment::Instance()->shareDir() +
			"/scautoloc/station.conf";
	}

	try {
		_config.pickLogFile = configGetString("autoloc.pickLog");
	}
	catch (...) {}

	try {
		_config.pickLogEnable =
			configGetBool("autoloc.pickLogEnable");
	}
	catch (...) {
		_config.pickLogEnable = false;
	}
	if ( ! _config.pickLogEnable ) {
		_config.pickLogFile = "";
	}

	try {
		_config.amplTypeSNR =
			configGetString("autoloc.amplTypeSNR");
	}
	catch (...) {}

	try {
		_config.amplTypeAbs =
			configGetString("autoloc.amplTypeAbs");
	}
	catch (...) {}

	try {
		_stationLocationFile =
			configGetString("autoloc.stationLocations");
	}
	catch (...) {}

	// support deprecated configuration, deprecated since 2020-11-13
	try {
		_config.locatorProfile =
			configGetString("autoloc.locator.profile");
		SEISCOMP_ERROR(
			"autoloc.locator.profile is deprecated."
		        " Use locator.profile instead!");
	}
	catch (...) {}

	// override deprecated configuration if value is set
	try {
		_config.locatorProfile =
			configGetString("locator.profile");
	}
	catch (...) {}

	try {
		_config.playback =
			configGetBool("autoloc.playback");
	}
	catch ( ... ) {}

	try {
		_config.test =
			configGetBool("autoloc.test");
	}
	catch ( ... ) {}

	_config.pickLogFile = Environment::Instance()->absolutePath(
					_config.pickLogFile);

	_stationLocationFile = Environment::Instance()->absolutePath(
					_stationLocationFile);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::init()
{
	if ( ! Client::Application::init() )
		return false;

	_inputPicks = addInputObjectLog("pick");
	_inputAmps = addInputObjectLog("amplitude");
	_inputOrgs = addInputObjectLog("origin");
	_outputOrgs = addOutputObjectLog("origin", primaryMessagingGroup());

	SEISCOMP_INFO("Starting Autoloc");
	setConfig(&Client::Application::configuration());

	_config.agencyID = agencyID();
	_config.check(); // only prints warnings, no show stopper
	setConfig(_config);

	dumpConfig();

	if ( ! initInventory() )
		return false;

	if ( ! _config.pickLogFile.empty() ) {
		setPickLogFilePrefix(_config.pickLogFile);
	}

	if ( _config.playback ) {
		if ( _inputFileEP.empty() ) {
			// XML playback, set timer to 1 sec
			SEISCOMP_DEBUG("Playback mode - enable timer of 1 sec");
			enableTimer(1);
		}
	}
	else {
		// Read past preferred origins in case we missed something
		readPastEvents();

		if ( _wakeUpTimout > 0 ) {
			SEISCOMP_DEBUG("Enable timer of %d secs", _wakeUpTimout);
			enableTimer(_wakeUpTimout);
		}
	}
	
	return Autoloc::Autoloc3::init();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::initInventory()
{
	if ( _stationLocationFile.empty() ) {
		SEISCOMP_DEBUG("Initializing station inventory from DB");
		inventory = Client::Inventory::Instance()->inventory();
	}
	else {
		SEISCOMP_DEBUG_S(
			"Initializing station inventory from file '" +
			_stationLocationFile + "'");
		inventory = DataModel::inventoryFromStationLocationFile(
				_stationLocationFile);
	}

	if ( ! inventory ) {
		SEISCOMP_ERROR("No station inventory!");
		return false;
	}

	Autoloc::Autoloc3::setInventory(inventory.get());

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void AutolocApp::readPicksForOrigin(const Seiscomp::DataModel::Origin *origin)
{
	using namespace Seiscomp::DataModel;

	DatabaseIterator it =
		query()->getPicks(origin->publicID());

	std::map<std::string, Pick*> picks;
	std::map<std::string, Amplitude*> ampls;

	for ( ; it.get() != nullptr; ++it ) {
		Pick *pick = Pick::Cast(it.get());
		if ( pick )
			picks[pick->publicID()] = pick;
	}
	it.close();

	it = query()->getAmplitudesForOrigin(origin->publicID());

	// Only keep amplitudes of needed type and only if they refer to a
	// registered pick.
	for ( ; it.get() != nullptr; ++it ) {
		Amplitude *ampl = Amplitude::Cast(it.get());
		if ( ! ampl ) {
			continue;
		}

		if (picks.find(ampl->pickID()) == picks.end())
		{
			delete ampl;
			continue;
		}

		if (ampl->type() != _config.amplTypeSNR &&
		    ampl->type() != _config.amplTypeAbs )
		{
			delete ampl;
			continue;
		}

		ampls[ampl->publicID()] = ampl;
	}
	it.close();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void AutolocApp::readPastEvents()
{
	using namespace Seiscomp::DataModel;

	if ( _keepEventsTimeSpan <= 0 || ! query() ) return;

	SEISCOMP_DEBUG(
		"readPastEvents: reading %d seconds of events",
		_keepEventsTimeSpan);

	typedef std::list<OriginPtr> OriginList;
	typedef std::set<std::string> PickIds;

	// Fetch all past events out of the database. The endtime is
	// probably in the future but because of timing differences between
	// different computers: safety first!
	Core::Time now = Core::Time::GMT();
	DatabaseIterator it =
		query()->getPreferredOrigins(
			now - Core::TimeSpan(_keepEventsTimeSpan),
		        now + Core::TimeSpan(_keepEventsTimeSpan), "");

	OriginList preferredOrigins;
	PickIds pickIds;

	// Store all preferred origins
	for ( ; it.get() != NULL; ++it ) {
		OriginPtr o = Origin::Cast(it.get());
		if ( o ) preferredOrigins.push_back(o);
	}
	it.close();

	// Store all pickIDs of all origins and remove duplicates
	for ( OriginList::iterator it = preferredOrigins.begin();
	      it != preferredOrigins.end(); ++it ) {
		OriginPtr origin = *it;
		if ( origin->arrivalCount() == 0 ) {
			query()->loadArrivals(it->get());
			for ( size_t i = 0; i < origin->arrivalCount(); ++i )
				pickIds.insert(origin->arrival(i)->pickID());
		}

		SEISCOMP_DEBUG_S("read past origin "+origin->publicID());

		// Feed it!
		//feedOrigin(it->get());
	}

	// Read all picks out of the database
	for ( PickIds::iterator it = pickIds.begin();
	      it != pickIds.end(); ++it ) {

		ObjectPtr obj = query()->getObject(Pick::TypeInfo(), *it);
		if ( !obj ) continue;
		PickPtr pick = Pick::Cast(obj);
		if ( !pick ) continue;

		// Feed it!
		//feedPick(pick.get());
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::runFromXMLFile(const char *fname)
{
	using namespace Seiscomp::DataModel;
	using namespace Seiscomp::Core;

	Seiscomp::IO::XMLArchive ar;

	if ( ! ar.open(fname)) {
		SEISCOMP_ERROR("unable to open XML file '%s'", fname);
		return false;
	}

	ar >> ep;
	ar.close();

	SEISCOMP_INFO("read event parameters from XML");
	SEISCOMP_INFO("  number of picks:      %ld", ep->pickCount());
	SEISCOMP_INFO("  number of amplitudes: %ld", ep->amplitudeCount());
	SEISCOMP_INFO("  number of origins:    %ld", ep->originCount());

	objectQueue.fill(ep.get());

	// clear ep
	// XXX No, we don't need to clear ep. If we keep the objects in ep
	// then we can to some degree simulate database access. See
	// loadAmplitude()/loadPick()
/*
	while (ep->pickCount() > 0)
		ep->removePick(0);
	while (ep->amplitudeCount() > 0)
		ep->removeAmplitude(0);
	while (ep->originCount() > 0)
		ep->removeOrigin(0);
*/

	if (_playbackSpeed > 0) {
		SEISCOMP_DEBUG("playback speed factor %g", _playbackSpeed);
	}

	objectsStartTime = playbackStartTime = Time::GMT();
	objectCount = 0;
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::runFromEPFile(const char *fname)
{
	using namespace Seiscomp::DataModel;
	using namespace Seiscomp::Core;

	Seiscomp::IO::XMLArchive ar;

	if ( ! ar.open(fname) ) {
		SEISCOMP_ERROR("unable to open XML file: %s", fname);
		return false;
	}

	ar >> ep;
	ar.close();

	if ( ! ep ) {
		SEISCOMP_ERROR("No event parameters found: %s", fname);
		return false;
	}

	SEISCOMP_INFO("read event parameters from XML");
	SEISCOMP_INFO("  number of picks:      %ld", ep->pickCount());
	SEISCOMP_INFO("  number of amplitudes: %ld", ep->amplitudeCount());
	SEISCOMP_INFO("  number of origins:    %ld", ep->originCount());

	objectQueue.fill(ep.get());

	// process the objects and write the resulting ep to stdout

	while ( !objectQueue.empty() && !isExitRequested() ) {
		PublicObjectPtr o = objectQueue.front();

		objectQueue.pop();
		addObject("", o.get());
		++objectCount;
	}

	// _flush();
	report();

	ar.create("-");
	ar.setFormattedOutput(true);
	ar << ep;
	ar.close();

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::run()
{
	if ( ! _inputFileEP.empty() )
		return runFromEPFile(_inputFileEP.c_str());

	// XML playback: first fill object queue, then run()
	if ( _config.playback && _inputFileXML.size() > 0) {
		runFromXMLFile(_inputFileXML.c_str());
		return Application::run();
	}

	// normal online mode
	if ( ! Autoloc::Autoloc3::config().offline )
		return Application::run();

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void AutolocApp::done() {
	_exitRequested = true;
	Autoloc::Autoloc3::shutdown();
	Application::done();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void AutolocApp::handleTimeout()
{
	using namespace Seiscomp::DataModel;

	if ( ! _config.playback || _inputFileXML.empty() ) {
		report();
		return;
	}


	// The following is executed only during XML playback.

	while ( ! objectQueue.empty()) {

		if (isExitRequested())
			break;

		PublicObjectPtr optr = objectQueue.front();
		PublicObject* o = optr.get();

		// retrieve the creationTime...
		Core::Time t;
		if (Pick::Cast(o))
			t = Pick::Cast(o)->creationInfo().creationTime();
		else if (Amplitude::Cast(o))
			t = Amplitude::Cast(o)->creationInfo().creationTime();
		else if (Origin::Cast(o))
			t = Origin::Cast(o)->creationInfo().creationTime();
		else
			continue;

		// at the first object:
		if (objectCount == 0)
			objectsStartTime = t;

		if (_playbackSpeed > 0) {
			double dt = t - objectsStartTime;
			Seiscomp::Core::TimeSpan dp = dt/_playbackSpeed;
			t = playbackStartTime + dp;
			if (Seiscomp::Core::Time::GMT() < t)
				// until next handleTimeout() call
				break;
		}
		// otherwise no speed limit :)

		objectQueue.pop();
		addObject("", o);
		objectCount++;
	}

	if (objectQueue.empty())
		quit();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void AutolocApp::handleAutoShutdown()
{
	SEISCOMP_DEBUG("shutdown");
	Autoloc::Autoloc3::shutdown();
	Seiscomp::Client::Application::handleAutoShutdown();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Seiscomp::DataModel::Pick*
AutolocApp::loadPick(const std::string &pickID)
{
	using namespace Seiscomp::DataModel;

	SEISCOMP_DEBUG_S("LOADING PICK "+pickID);

	Object *obj = query()->getObject(Pick::TypeInfo(), pickID);
	if ( ! obj )
		return NULL;

	Pick *pick = Pick::Cast(obj);
	if ( ! pick )
		return NULL;

	return pick;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Seiscomp::DataModel::Amplitude*
AutolocApp::loadAmplitude(
	const std::string &pickID,
	const std::string &amplitudeType)
{
	SEISCOMP_DEBUG_S("LOADING AMPL "+pickID+" "+amplitudeType);
	if (query())
		return query()->getAmplitude(pickID, amplitudeType);
	return nullptr;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
size_t
AutolocApp::loadPicksAndAmplitudesForOrigin(
	const Seiscomp::DataModel::Origin *scorigin,
	std::vector<PickAmplitudeSet> &pa)
{
	using namespace Seiscomp::DataModel;

	SEISCOMP_DEBUG_S(
		"Loading from DB picks/amplitudes for origin" +
		scorigin->publicID());

	for (size_t i=0; i<scorigin->arrivalCount(); i++) {
		const Arrival *arr = scorigin->arrival(i);
		const std::string &pickID(arr->pickID());

		Pick *pick=nullptr;
		Amplitude *snr=nullptr, *abs=nullptr;

		if (query()) {
			// Load objects from database.
			pick = loadPick(pickID);
			snr = loadAmplitude(
				pickID, _config.amplTypeSNR);
			abs = loadAmplitude(
				pickID, _config.amplTypeAbs);
		}
		else {
			// No database (e.g. XML playback)
			//
			// Try to find objects in EventParameters.
			// This is of course quite expensive as we
			// have to visit all amplitudes.
			pick = Pick::Find(pickID);
			for (size_t k=0; k<ep->amplitudeCount(); k++) {
				Amplitude *a = ep->amplitude(k);
				if (a->pickID() != pickID)
					continue;

				if (snr==nullptr &&
				    a->type() == _config.amplTypeSNR) {
					snr = a;
					continue;
				}

				if (abs==nullptr &&
				    a->type() == _config.amplTypeAbs) {
					abs = a;
					continue;
				}

				if (snr!=nullptr && abs!=nullptr)
					break;
			}
		}

		if (pick) {
			pa.push_back(
				PickAmplitudeSet(pick, snr, abs));
		}
	}

	SEISCOMP_DEBUG("Loaded %ld picks/amplitudes", pa.size());

	return pa.size();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static bool manual(const Seiscomp::DataModel::Origin *origin)
{
	try {
		switch (origin->evaluationMode()) {
		case Seiscomp::DataModel::MANUAL:
			return true;
		default:
			break;
		}
	}
	catch ( Seiscomp::Core::ValueException & ) {}
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static bool preliminary(const Seiscomp::DataModel::Origin *origin) {
	try {
		switch (origin->evaluationStatus()) {
		case Seiscomp::DataModel::PRELIMINARY:
			return true;
		default:
			break;
		}
	}
	catch ( Seiscomp::Core::ValueException & ) {}
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void AutolocApp::addObject(
	const std::string& parentID, Seiscomp::DataModel::Object* o)
{
	using namespace Seiscomp::DataModel;

//	Core::Time now = Core::Time::GMT();

	// // logging of all PublicObject::publicID's
	// PublicObject *po = PublicObject::Cast(o);
	// if (po == NULL)
	// 	return;
	// SEISCOMP_DEBUG("adding  %-12s %s", po->className(), po->publicID().c_str());

	Pick *pick = Pick::Cast(o);
	if (pick) {
		sync(pick->creationInfo().creationTime());
		logObject(_inputPicks, now());
		feed(pick);
		return;
	}

	Amplitude *amplitude = Amplitude::Cast(o);
	if (amplitude) {
		sync(amplitude->creationInfo().creationTime());
		logObject(_inputAmps, now());
		feed(amplitude);
		return;
	}

	Origin *origin = Origin::Cast(o);
	if (origin) {
		sync(origin->creationInfo().creationTime());
		logObject(_inputOrgs, now());
		feed(origin);
		return;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void logBlockedAgency(const Seiscomp::DataModel::Pick *scpick) {
// TODO
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::feed(Seiscomp::DataModel::Pick *scpick) {

	if ( isAgencyIDBlocked(objectAgencyID(scpick)) ) {
		SEISCOMP_DEBUG_S(
			"Blocked pick from agency '" +
			objectAgencyID(scpick) + "'");
		return false;
	}

	SEISCOMP_DEBUG(
		"pick '%s' from agency '%s'",
		scpick->publicID().c_str(), objectAgencyID(scpick).c_str());


	if ( _config.offline )
		timeStamp();

	if ( ! Autoloc::Autoloc3::feed(scpick))
		return false;

	if ( _config.offline )
		// _flush();
		report();

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::feed(Seiscomp::DataModel::Amplitude *scampl)
{
	const std::string &amplID = scampl->publicID();

	if (_inputFileXML.size() || _inputFileEP.size()) {
		try {
			const Core::Time &creationTime =
				scampl->creationInfo().creationTime();
			sync(creationTime);
		}
		catch(...) {
			SEISCOMP_WARNING_S(
				"Amplitude "+amplID+" without creation time!");
		}
	}

	if (objectAgencyID(scampl) != _config.agencyID) {
		if ( isAgencyIDBlocked(objectAgencyID(scampl)) ) {
			SEISCOMP_DEBUG_S("Blocked amplitude from agency '" +
					objectAgencyID(scampl) + "'");
			return false;
		}
		SEISCOMP_DEBUG("ampl '%s' from agency '%s'",
			      amplID.c_str(), objectAgencyID(scampl).c_str());
	}

	Autoloc::Autoloc3::feed(scampl);
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::feed(Seiscomp::DataModel::Origin *scorigin)
{
	if ( ! scorigin ) {
		SEISCOMP_ERROR("This should never happen: origin is null");
		return false;
	}

	if ( isAgencyIDBlocked(objectAgencyID(scorigin)) ) {
		SEISCOMP_INFO_S(
			"Ignored origin from " +
			objectAgencyID(scorigin) + " because "
			"this agency ID is blocked");
		return false;
	}

	if ( ! Autoloc::Autoloc3::isTrustedOrigin(scorigin) )
		return false;

	SEISCOMP_INFO(
		"got %s origin %s  agency: %s",
		manual(scorigin) ? "manual" : "automatic",
		scorigin->publicID().c_str(),
		objectAgencyID(scorigin).c_str());

	const bool ownOrigin = objectAgencyID(scorigin) == _config.agencyID;

	if (ownOrigin) {
		// Collect all picks required for processing the origin
		// before feeding it to Autoloc.

		// This may need access to the database and is therefore
		// in the application class.

		std::vector<PickAmplitudeSet> pa;
		loadPicksAndAmplitudesForOrigin(scorigin, pa);

		bool wasProcessingEnabled =
			Autoloc::Autoloc3::isProcessingEnabled();
		Autoloc::Autoloc3::setProcessingEnabled(false);
		for (PickAmplitudeSet &s: pa) {
// FIXME TEMP
//			Autoloc::Autoloc3::feed(s.pick.get());
//			if (s.amplitudeSNR)
//				Autoloc::Autoloc3::feed(s.amplitudeSNR.get());
//			if (s.amplitudeAbs)
//				Autoloc::Autoloc3::feed(s.amplitudeAbs.get());
		}
		// restore processing state
		Autoloc::Autoloc3::setProcessingEnabled(wasProcessingEnabled);
	}

	Autoloc::Autoloc3::feed(scorigin);

	if ( _config.offline )
		report();

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool AutolocApp::_report(Seiscomp::DataModel::Origin *scorigin)
{
	using namespace Seiscomp::DataModel;

	// Log object flow
	logObject(_outputOrgs, now());

	CreationInfo ci;
	ci.setAgencyID(_config.agencyID);
	ci.setAuthor(author());
	ci.setCreationTime(now());
	scorigin->setCreationInfo(ci);

	if (_config.offline || _config.test) {
		SEISCOMP_INFO (
			"Origin %s not sent (test/offline mode)",
			scorigin->publicID().c_str());

		// global EventParameters instance
		if ( ! _inputFileEP.empty())
			ep->add(scorigin);

		return true;
	}

	EventParameters ep;
	bool wasEnabled = Notifier::IsEnabled();
	Notifier::Enable();
	ep.add(scorigin);
	Notifier::SetEnabled(wasEnabled);
	NotifierMessagePtr nmsg = Notifier::GetMessage(true);
	if (connection()->send(nmsg.get())) {
		SEISCOMP_INFO(
			"Origin %s sent to the message group: %s",
			scorigin->publicID().c_str(),
			primaryMessagingGroup().c_str());
	}
	else {
		SEISCOMP_ERROR(
			"Sending of origin %s failed with error: %s",
			scorigin->publicID().c_str(),
			connection()->lastError().toString());
	}

	if (preliminary(scorigin)) {
		SEISCOMP_INFO(
			"Sent preliminary origin %s (heads up)",
			scorigin->publicID().c_str());

		// create and send journal entry
		std::string str;
		try {
			str = scorigin->evaluationStatus().toString();
		}
		catch ( Core::ValueException & ) {}

		if ( !str.empty() ) {
			JournalEntryPtr journalEntry = new JournalEntry;
			journalEntry->setAction("OrgEvalStatOK");
			journalEntry->setObjectID(scorigin->publicID());
			journalEntry->setSender(SCCoreApp->author().c_str());
			journalEntry->setParameters(str);
			journalEntry->setCreated(Core::Time::GMT());

			NotifierMessagePtr jm = new NotifierMessage;
			jm->attach(new Notifier(
				Journaling::ClassName(),
				OP_ADD,
				journalEntry.get()));

			if ( connection()->send(jm.get()) ) {
				SEISCOMP_DEBUG(
					"Sent origin journal entry for origin %s to the message group: %s",
					scorigin->publicID().c_str(),
					primaryMessagingGroup().c_str());
			}
			else {
				SEISCOMP_ERROR(
					"Sending of origin journal entry failed with error: %s",
					connection()->lastError().toString());
			}
		}
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}  // namespace Autoloc

}  // namespace Seiscomp
