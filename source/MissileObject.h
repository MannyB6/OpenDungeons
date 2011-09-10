#ifndef MISSILEOBJECT_H
#define MISSILEOBJECT_H

#include <deque>
#include <string>

#include <Ogre.h>
#include <semaphore.h>

#include "ActiveObject.h"
#include "AnimatedObject.h"

class GameMap;

class MissileObject: public ActiveObject, public AnimatedObject
{
    public:
        MissileObject(GameMap& gameMap);
        MissileObject(const std::string& nMeshName, const Ogre::Vector3& nPosition, GameMap& gameMap);
        void initialize();

        void setPosition(Ogre::Real x, Ogre::Real y, Ogre::Real z);
        void setPosition(const Ogre::Vector3& v);
        Ogre::Vector3 getPosition();

        void createMesh();
        void destroyMesh();
        void deleteYourself();

        std::string meshName;
        std::string getOgreNamePrefix()
        {
            return "";
        }
        const std::string& getName() const
        {
            return name;
        }
        std::string name;
        
        virtual bool doUpkeep();
        virtual void stopWalking();

        static sem_t missileObjectUniqueNumberLockSemaphore;

    private:
        GameMap& gameMap;
        std::deque<Ogre::Vector3> walkQueue;
        Ogre::Vector3 position;
        sem_t positionLockSemaphore;
        bool meshesExist;
};

#endif
