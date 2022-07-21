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



#ifndef SEISCOMP_LIBAUTOLOC_PICKQUEUE_H_INCLUDED
#define SEISCOMP_LIBAUTOLOC_PICKQUEUE_H_INCLUDED

#include <queue>
#include <seiscomp/datamodel/pick.h>
#include <seiscomp/datamodel/amplitude.h>

namespace Seiscomp {

namespace DataModel {


// The purpose of the PickQueue is to receive Pick and Amplitude objects e.g.
// from the messaging and delay the processing until not only the pick but
// also required amplitudes are all available.

struct PickAndAmplitudes {
	PickPtr	     pick;
	AmplitudePtr amplAbs;
	AmplitudePtr amplSNR;
};


class PickQueue {

	public:
		void feed(Pick*);
		void feed(Amplitude*);

		PickAndAmplitudes front();

		void pop();

		bool empty() const;

		size_t size() const;

	private:
		std::queue<PickAndAmplitudes> q;
};


} // namespace DataModel

} // namespace Seiscomp

#endif
