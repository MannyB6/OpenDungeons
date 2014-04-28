/*
 *  Copyright (C) 2011-2014  OpenDungeons Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CULLINGMANAGER_H_
#define CULLINGMANAGER_H_

#include "CameraManager.h"
#include "MortuaryQuad.h"
#include "SlopeWalk.h"
#include "Vector3i.h"

#include <OgreRay.h>
#include <OgrePlane.h>
#include <set>

class CameraManager;
class GameMap;

extern const int mPrecisionDigits;
extern const int Unit;

class CullingManager
{
    friend class GameMap;

public:
    CullingManager(CameraManager*);

    void startCreatureCulling();
    void startTileCulling();

    void stopCreatureCulling();
    void stopTileCulling();

    bool getIntersectionPoints();

    void hideAllTiles();

    int cullCreatures();
    int cullTiles();

    bool onFrameStarted();
    bool onFrameEnded();

    // set the new tiles
    int bashAndSplashTiles(int);
    int newBashAndSplashTiles(int);

    void sort(Vector3i& p1, Vector3i& p2, bool sortByX);



private:
    std::set<Creature*>*  mCurrentVisibleCreatures;
    std::set<Creature*>*  mPreviousVisibleCreatures;

    std::set<Creature*> mCreaturesSet[2];

    SlopeWalk mWalk, oldWalk;
    
    MortuaryQuad mMyCullingQuad;
    Ogre::Vector3 mOgreVectorsArray[4];
    Vector3i mTop, mBottom, mMiddleLeft, mMiddleRight;
    Vector3i mOldTop, mOldBottom, mOldMiddleLeft, mOldMiddleRight;

    bool mFirstIter;
    CameraManager* mCm;

    Ogre::Plane mMyplanes[6];
    Ogre::Ray mMyRay[4];

    bool mCullCreaturesFlag;
    bool mCullTilesFlag;
};

#endif // CULLINGMANAGER_H_
