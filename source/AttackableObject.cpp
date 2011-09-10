#include "AttackableObject.h"

AttackableObject::AttackableObject() :
    gameMap(NULL)
{

}

std::vector<AttackableObject*> AttackableObject::removeDeadObjects(
        const std::vector<AttackableObject*> &objects)
{
    std::vector<AttackableObject*> ret;
    for(unsigned int i = 0, size = objects.size(); i < size; ++i)
    {
        if (objects[i]->getHP(NULL) > 0.0)
            ret.push_back(objects[i]);
    }

    return ret;
}
