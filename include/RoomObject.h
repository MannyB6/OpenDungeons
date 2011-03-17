#ifndef ROOMOBJECT_H
#define ROOMOBJECT_H

#include "Room.h"
#include "ActiveObject.h"
#include "AnimatedObject.h"
#include <string>
#include <istream>
#include <ostream>

class RoomObject: public ActiveObject, public AnimatedObject
{
    public:
        RoomObject(Room *nParentRoom, std::string nMeshName);

        std::string getName();
        std::string getMeshName();

        Room* getParentRoom();

        void createMesh();
        void destroyMesh();
        void deleteYourself();

        std::string getOgreNamePrefix();

        static std::string getFormat();
        friend std::ostream& operator<<(std::ostream& os, RoomObject *o);
        friend std::istream& operator>>(std::istream& is, RoomObject *o);

        double x, y;
        double rotationAngle;

    private:
        Room *parentRoom;
        bool meshExists;
        std::string name, meshName;
};

#endif

