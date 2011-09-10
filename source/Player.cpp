#include "Player.h"
#include "Functions.h"
#include "ServerNotification.h"
#include "Creature.h"
#include "GameMap.h"
#include "Seat.h"
#include "Weapon.h"
#include "ClientNotification.h"
#include "RenderRequest.h"
#include "RenderManager.h"
#include "Socket.h"

Player::Player() :
        newRoomType(Room::nullRoomType),
        newTrapType(Trap::nullTrapType),
        gameMap(NULL),
        seat(NULL),
        nick(""),
        hasAI(false)
{
}

/** \brief A simple accessor function to return the number of creatures this player is holding in their hand.
 *
 */
unsigned int Player::numCreaturesInHand() const
{
    return creaturesInHand.size();
}

/** \brief A simple accessor function to return a pointer to the i'th creature in the players hand.
 *
 */
Creature* Player::getCreatureInHand(int i)
{
    return creaturesInHand[i];
}

/** \brief A simple accessor function to return a pointer to the i'th creature in the players hand. (const version)
 *
 */
const Creature* Player::getCreatureInHand(int i) const
{
    return creaturesInHand[i];
}

/** \brief A simple mutator function to put the given creature into the player's hand, note this should NOT be called directly for creatures on the map, for that you should use pickUpCreature() instead.
 *
 */
void Player::addCreatureToHand(Creature *c)
{
    if (creaturesInHand.empty())
    {
        creaturesInHand.push_back(c);
    }
    else
    {
        //creaturesInHand.push_front(c);
        // Since vectors have no push_front method we need to move all of the elements in the vector back one and then add this one to the beginning.
        creaturesInHand.push_back(NULL);
        for (unsigned int j = creaturesInHand.size() - 1; j > 0; --j)
            creaturesInHand[j] = creaturesInHand[j - 1];

        creaturesInHand[0] = c;
    }
}

/*! \brief Check to see if it is the user or another player picking up the creature and act accordingly.
 *
 * This function takes care of all of the operations required for a player to
 * pick up a creature.  If the player is the user we need to move the creature
 * oncreen to the "hand" as well as add the creature to the list of creatures
 * in our own hand, this is done by setting moveToHand to true.  If move to
 * hand is false we just hide the creature (and stop its AI, etc.), rather than
 * making it follow the cursor.
 */
void Player::pickUpCreature(Creature *c)
{
    assert(gameMap != NULL);
    if (c->getHP() <= 0.0)
        return;

    // Stop the creature walking and take it off the gameMap to prevent the AI from running on it.
    gameMap->removeCreature(c);
    c->clearDestinations();
    c->clearActionQueue();

    // Start tracking this creature as being in this player's hand
    addCreatureToHand(c);

    // Destroy the creature's visual debugging entities if it has them
    if (c->getHasVisualDebuggingEntities())
    {
        c->destroyVisualDebugEntities();
    }

    if (Socket::serverSocket != NULL || Socket::clientSocket != NULL)
    {
        // Inform the clients
        if (Socket::serverSocket != NULL)
        {
            // Place a message in the queue to inform the clients that we picked up this creature
            ServerNotification *serverNotification = new ServerNotification;
            serverNotification->type = ServerNotification::creaturePickUp;
            serverNotification->cre = c;
            serverNotification->player = this;

            queueServerNotification(serverNotification);
        }

        // If it is actually the user picking up a creature we move the scene node and inform
        // the server, otherwise we just hide the creature from the map.
        if (this == gameMap->getLocalPlayer())
        {
            // Send a render request to move the crature into the "hand"
            RenderRequest *request = new RenderRequest;
            request->type = RenderRequest::pickUpCreature;
            request->p = c;

            // Add the request to the queue of rendering operations to be performed before the next frame.
            RenderManager::queueRenderRequest(request);

            if (Socket::clientSocket != NULL)
            {
                // Send a message to the server telling it we picked up this creature
                ClientNotification *clientNotification = new ClientNotification;
                clientNotification->type = ClientNotification::creaturePickUp;
                clientNotification->p = c;
                clientNotification->p2 = this;

                sem_wait(&ClientNotification::clientNotificationQueueLockSemaphore);
                ClientNotification::clientNotificationQueue.push_back(clientNotification);
                sem_post(&ClientNotification::clientNotificationQueueLockSemaphore);

                sem_post(&ClientNotification::clientNotificationQueueSemaphore);
            }
        }
        else // it is just a message indicating another player has picked up a creature
        {
            // Hide the creature
            c->destroyMesh();
        }

    }
}

/** \brief A simple mutator function to remove a creature from the player's hand, note this should NOT be called directly for creatures on the map, for that you should use dropCreature() instead.
 *
 */
void Player::removeCreatureFromHand(int i)
{
    creaturesInHand.erase(creaturesInHand.begin() + i);
}

/*! \brief Check to see the first creatureInHand can be dropped on Tile t and do so if possible.
 *
 */
bool Player::dropCreature(Tile* t, unsigned int index)
{
    // if we have a creature to drop
    if (!creaturesInHand.empty())
    {
        Creature *tempCreature = creaturesInHand[index];

        // if the tile is a valid place to drop a creature
        //FIXME:  This could be a race condition, if the tile state changes on the server before the client knows about it.
        if (t->getFullness() < 1 && ((tempCreature->digRate > 0.1
                && t->getType() == Tile::dirt) || (t->getType()
                == Tile::claimed && t->getColor() == gameMap->getLocalPlayer()->getSeat()->getColor())))
        {
            // Add the creature to the map
            Creature *c = creaturesInHand[index];
            creaturesInHand.erase(creaturesInHand.begin() + index);
            gameMap->addCreature(c);

            // If this is the result of another player dropping the creature it is currently not visible so we need to create a mesh for it
            //cout << "\nthis:  " << this << "\nme:  " << gameMap->getLocalPlayer() << endl;
            //cout.flush();
            if (this != gameMap->getLocalPlayer())
            {
                c->createMesh();
                c->weaponL->createMesh();
                c->weaponR->createMesh();
            }
            else // This is the reult of the player on the local computer dropping the creature
            {
                // Send a render request to rearrange the creatures in the hand to move them all forward 1 place
                RenderRequest *request = new RenderRequest;
                request->type = RenderRequest::dropCreature;
                request->p = c;
                request->p2 = this;

                // Add the request to the queue of rendering operations to be performed before the next frame.
                RenderManager::queueRenderRequest(request);
            }

			c->setPosition(static_cast<Ogre::Real>(t->x), 
				static_cast<Ogre::Real>(t->y), 0.0);

            if (this == gameMap->getLocalPlayer() || hasAI)
            {
                if (Socket::serverSocket != NULL)
                {
                    // Place a message in the queue to inform the clients that we dropped this creature
                    ServerNotification *serverNotification =
                            new ServerNotification;
                    serverNotification->type = ServerNotification::creatureDrop;
                    serverNotification->player = this;
                    serverNotification->tile = t;

                    queueServerNotification(serverNotification);
                }
                else
                {
                    if (Socket::clientSocket != NULL && this == gameMap->getLocalPlayer())
                    {
                        // Send a message to the server telling it we dropped this creature
                        ClientNotification *clientNotification = new ClientNotification;
                        clientNotification->type = ClientNotification::creatureDrop;
                        clientNotification->p = this;
                        clientNotification->p2 = t;

                        sem_wait(&ClientNotification::clientNotificationQueueLockSemaphore);
                        ClientNotification::clientNotificationQueue.push_back(clientNotification);
                        sem_post(&ClientNotification::clientNotificationQueueLockSemaphore);

                        sem_post(&ClientNotification::clientNotificationQueueSemaphore);
                    }
                }
            }

            return true;
        }
    }

    return false;
}

void Player::rotateCreaturesInHand(int n)
{
    Creature *tempCreature;

    // If there are no creatures or only one creature in our hand, rotation doesn't change the order.
    if (creaturesInHand.size() < 2)
        return;

    for (unsigned int i = 0; i < (unsigned int) fabs((double) n); ++i)
    {
        if (n > 0)
        {
            tempCreature = creaturesInHand.back();

            //creaturesInHand.push_front(tempCreature);
            // Since vectors have no push_front method we need to move all of the elements in the vector back one and then add this one to the beginning.
            for (unsigned int j = creaturesInHand.size() - 1; j > 0; --j)
                creaturesInHand[j] = creaturesInHand[j - 1];

            creaturesInHand[0] = tempCreature;

        }
        else
        {
            tempCreature = creaturesInHand.front();
            creaturesInHand.erase(creaturesInHand.begin());
            creaturesInHand.push_back(tempCreature);
        }
    }

    // Send a render request to move the crature into the "hand"
    RenderRequest *request = new RenderRequest;
    request->type = RenderRequest::rotateCreaturesInHand;

    // Add the request to the queue of rendering operations to be performed before the next frame.
    RenderManager::queueRenderRequest(request);
}
