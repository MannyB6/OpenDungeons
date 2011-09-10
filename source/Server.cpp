#include <string>
#include <ctime>

#include "Socket.h"
#include "ODFrameListener.h"
#include "Network.h"
#include "ChatMessage.h"
#include "Functions.h"
#include "Sleep.h"
#include "ServerNotification.h"
#include "Player.h"
#include "Tile.h"
#include "MapLight.h"
#include "GameMap.h"
#include "ProtectedObject.h"
#include "Creature.h"
#include "ODApplication.h"
#include "LogManager.h"
#include "Seat.h"

/*! \brief A thread function which runs on the server and listens for new connections from clients.
 *
 * A single instance of this thread is spawned by running the "host" command
 * from the in-game console.  The thread then binds annd listens on the
 * specified port and when clients connect a new socket, and a
 * clientHandlerThread are spawned to handle communications with that client.
 * This function currently has no way of breaking out of its primary loop, so
 * once it is started it never exits until the program is closed.
 */
// THREAD - This function is meant to be called by pthread_create.
void *serverSocketProcessor(void *p)
{
    Socket *sock = ((SSPStruct*) p)->nSocket;
    Socket *curSock;
    ODFrameListener *frameListener = ((SSPStruct*) p)->nFrameListener;
    delete (SSPStruct*) p;
    p = NULL;

    // Set up the socket to listen on the specified port
    if (!sock->create())
    {
        frameListener->commandOutput
                = "ERROR:  Server could not create server socket!";
        return NULL;
    }

    int port = ODApplication::PORT_NUMBER;
    if (!sock->bind(port))
    {
        frameListener->commandOutput
                += "ERROR:  Server could not bind to port!";
        return NULL;
    }

    // Listen for connectections and spawn a new socket+thread to handle them
    while (true)
    {
        //FIXME:  This function leaks memory as none of these structures are deleted.

        // Wait until a client connects
        if (!sock->listen())
        {
            frameListener->commandOutput += "ERROR:  Server could not listen!";
            return NULL;
        }

        // create a new socket to handle the connection with this client
        curSock = new Socket;
        sock->accept(*curSock);

        //FIXME:  Also need to remove this pointer from the vector when the connection closes.
        frameListener->clientSockets.push_back(curSock);

        pthread_t *clientThread = new pthread_t;
        CHTStruct *params = new CHTStruct;
        params->nSocket = curSock;
        params->nFrameListener = frameListener;
        pthread_create(clientThread, NULL, clientHandlerThread, (void*) params);
        frameListener->clientHandlerThreads.push_back(clientThread);
    }
    return NULL;
}

/*! \brief A helper function to pack a message into a packet to send over the network.
 *
 * This function packs the command and arguments to be sent over the network
 * into a return string which is then sent over the network.  This decouples
 * the encoding from the actual program code so changes in the wire protocol
 * are confined to this function and its sister function, parseCommand.
 */
std::string formatCommand(std::string command, std::string arguments)
{
    //FIXME:  Need to protect the ":" symbol with an escape sequence.
    return "<" + command + ":" + arguments + ">";
}

/*! \brief A helper function to unpack a message from a packet received over the network.
 *
 * This function unpacks the command and arguments sent over the network into
 * two strings.  This decouples the decoding from the actual program code so
 * changes in the wire protocol are confined to this function and its sister
 * function, formatCommand.
 */
bool parseCommand(std::string &command, std::string &commandName, std::string &arguments)
{
    //FIXME:  Need to protect the ":" symbol with an escape sequence.
    int index, index2;
    index = command.find("<");
    index2 = command.find(":");
    commandName = command.substr(index + 1, index2 - 1);
    index = index2;
    index2 = command.find(">");
    arguments = command.substr(index + 1, index2 - index - 1);
    command = command.substr(index2 + 1, command.length() - index2 + 1);
    //cout << "\n\n\nParse command:  " << command << "\n" << commandName << "\n" << arguments << "\n\n";

    return (command.length() > 0) ? true : false;
}

/*! \brief A helper function to unpack the argument of a chat command into a ChatMessage structure.
 *
 * Once a command recieved from the network and hase been parsed by
 * parseCommand, this function then takes the argument of that message and
 * further unpacks a username and a chat message.
 */
ChatMessage *processChatMessage(std::string arguments)
{
    int index = arguments.find(":");
    std::string messageNick = arguments.substr(0, index);
    std::string message = arguments.substr(index + 1, arguments.size() - index - 1);

    return new ChatMessage(messageNick, message, time(NULL));
}

/*! \brief The thread which decides what creatures will do and carries out their actions.
 *
 * Creature AI is currently done only on an individual basis.  The creatures
 * are looped over, calling each one's doTurn method in succession.  The
 * doTurn method is responsible for deciding what action is taken by the
 * creature in the upcoming turn.  Once a course of action has been decided
 * upon the doTurn method also moves the creature, sets its animation state,
 * adjusts the creature's HP, etc.
 *
 * Since the creature AI thread runs on the server, changes to the creature's
 * state must be communicated to the some or all clients (depending on fog of
 * war, etc).  This is accomplished by building a ServerNotification request
 * and placing it in the serverNotificationQueue.  Since the
 * serverNotificationProcessor will decide which clients should know about a
 * given event, we can simply generate a notification request for any state
 * change and dump it in the queue and not worry about which clients need to
 * know about it.
 */
// THREAD - This function is meant to be called by pthread_create.
void *creatureAIThread(void *p)
{
    GameMap* gameMap = static_cast<GameMap*>(p);
    double timeUntilNextTurn = 1.0 / ODApplication::turnsPerSecond;
    Ogre::Timer stopwatch;
    unsigned long int timeTaken;
    LogManager* logManager = LogManager::getSingletonPtr();

    while (true)
    {
        // Do a turn in the game
        stopwatch.reset();
        GameMap::turnNumber.lock();
        GameMap::turnNumber.rawSet(GameMap::turnNumber.rawGet() + 1);
        GameMap::turnNumber.unlock();

        // Place a message in the queue to inform the clients that a new turn has started
        try
        {
            ServerNotification *serverNotification = new ServerNotification;
            serverNotification->type = ServerNotification::turnStarted;

            queueServerNotification(serverNotification);
        }
        catch (bad_alloc&)
        {
            logManager->logMessage("ERROR:  bad alloc in creatureAIThread at turnStarted", Ogre::LML_CRITICAL);
            exit(1);
        }

        // Go to each creature and call their individual doTurn methods
        gameMap->doTurn();
        timeUntilNextTurn = 1.0 / ODApplication::turnsPerSecond;

        timeTaken = stopwatch.getMicroseconds();
        gameMap->previousLeftoverTimes.push_front((1e6 * timeUntilNextTurn
                - timeTaken) / (double) 1e6);


        // Sleep this thread if it is necessary to keep the turns from happening too fast
        if (1e6 * timeUntilNextTurn - timeTaken > 0)
        {
            if(logManager->getLogDetail() == Ogre::LL_BOREME)
            {
                logManager->logMessage("\nCreature AI finished " + Ogre::StringConverter::toString((int) (1e6
                    * timeUntilNextTurn - timeTaken), 9) + "us early.", Ogre::LML_TRIVIAL);
            }
            usleep(1e6 * timeUntilNextTurn - timeTaken);
        }
        else
        {
            if(logManager->getLogDetail() == Ogre::LL_BOREME)
            {
                logManager->logMessage("\nCreature AI finished " + Ogre::StringConverter::toString((int) (1e6
                    * timeUntilNextTurn - timeTaken), 9) + "us late.", Ogre::LML_TRIVIAL);
            }
        }

        if(logManager->getLogDetail() == Ogre::LL_BOREME)
        {
            std::stringstream ss;
            ss << "\nThe Creature AI thread took:  " << gameMap->creatureTurnsTime
                / (double) 1e6;
            ss << "\nThe misc upkeep thread took:  " << gameMap->miscUpkeepTime
                    / (double) 1e6;
            ss << "\n";
            logManager->logMessage(ss.str());
        }


        if (gameMap->previousLeftoverTimes.size() > 10)
            gameMap->previousLeftoverTimes.resize(10);
        //If requested, we finish the thread.
        if(ODFrameListener::getSingleton().getThreadStopRequested())
        {
            //Ogre::LogManager::getSingleton().logMessage("Stopped creature AI thread.", Ogre::LML_NORMAL);
            break;
        }
    }

    // Return something to make the compiler happy
    return NULL;
}

/*! \brief The thread which monitors the serverNotificationQueue for new events and informs the clients about them.
 *
 * This thread runs on the server and acts as a "consumer" on the
 * serverNotificationQueue.  It takes an event out of the queue, determines
 * which clients need to be informed about that particular event, and
 * dispacthes TCP packets to inform the clients about the new information.
 */
// THREAD - This function is meant to be called by pthread_create.
void *serverNotificationProcessor(void *p)
{
    ODFrameListener *frameListener = ((SNPStruct*) p)->nFrameListener;
    GameMap* gameMap = frameListener->getGameMap();
    delete (SNPStruct*) p;
    p = NULL;

    std::stringstream tempSS;
    Tile *tempTile;
    Player *tempPlayer;
    MapLight *tempMapLight;
    AnimatedObject *tempAnimatedObject;
    bool running = true;

    while (running)
    {
        // Wait until a message is put into the serverNotificationQueue
        sem_wait(&ServerNotification::serverNotificationQueueSemaphore);

        // Take a message out of the front of the notification queue
        sem_wait(&ServerNotification::serverNotificationQueueLockSemaphore);
        ServerNotification *event = ServerNotification::serverNotificationQueue.front();
        ServerNotification::serverNotificationQueue.pop_front();
        sem_post(&ServerNotification::serverNotificationQueueLockSemaphore);

        //FIXME:  This really should never happen but the queue does occasionally pop a NULL.
        //This is probably a bug somewhere else where a NULL is being place in the queue.
        if (event == NULL)
        {
            continue;
        }

        switch (event->type)
        {
            case ServerNotification::turnStarted:
                tempSS.str("");
                tempSS << GameMap::turnNumber.get();

                sendToAllClients(frameListener, formatCommand("newturn", tempSS.str()));
                break;

            case ServerNotification::animatedObjectAddDestination:
                tempSS.str("");
                tempSS << event->str << ":" << event->vec.x << ":"
                        << event->vec.y << ":" << event->vec.z;

                sendToAllClients(frameListener, formatCommand(
                        "animatedObjectAddDestination", tempSS.str()));
                break;

            case ServerNotification::animatedObjectClearDestinations:
                tempSS.str("");
                tempSS << event->ani->getName();
                sendToAllClients(frameListener, formatCommand(
                        "animatedObjectClearDestinations", tempSS.str()));
                break;

                //NOTE: this code is duplicated in clientNotificationProcessor
            case ServerNotification::creaturePickUp:
                tempSS.str("");
                tempSS << event->player->getNick() << ":" << event->cre->name;

                sendToAllClients(frameListener, formatCommand("creaturePickUp",
                        tempSS.str()));
                break;

                //NOTE: this code is duplicated in clientNotificationProcessor
            case ServerNotification::creatureDrop:
                tempPlayer = event->player;
                tempTile = event->tile;

                tempSS.str("");
                tempSS << tempPlayer->getNick() << ":" << tempTile->x << ":"
                        << tempTile->y;

                sendToAllClients(frameListener, formatCommand("creatureDrop",
                        tempSS.str()));
                break;

            case ServerNotification::setObjectAnimationState:
                tempSS.str("");
                tempAnimatedObject = (AnimatedObject*) event->p;
                tempSS << tempAnimatedObject->getName() << ":" << event->str
                        << ":" << (event->b ? "true" : "false");
                sendToAllClients(frameListener, formatCommand(
                        "setObjectAnimationState", tempSS.str()));
                break;

            case ServerNotification::setTurnsPerSecond:
                tempSS.str("");
                tempSS << ODApplication::turnsPerSecond;

                sendToAllClients(frameListener, formatCommand("turnsPerSecond",
                        tempSS.str()));
                break;

            case ServerNotification::tileFullnessChange:
                tempSS.str("");
                tempTile = event->tile;
                tempSS << tempTile->getFullness() << ":" << tempTile->x << ":"
                        << tempTile->y;

                sendToAllClients(frameListener, formatCommand(
                        "tileFullnessChange", tempSS.str()));
                break;

            case ServerNotification::addMapLight:
                tempMapLight = (MapLight*) event->p;
                tempSS.str("");
                tempSS << tempMapLight;
                sendToAllClients(frameListener, formatCommand("addmaplight",
                        tempSS.str()));
                break;

            case ServerNotification::removeMapLight:
                tempMapLight = (MapLight*) event->p;
                sendToAllClients(frameListener, formatCommand("removeMapLight",
                        tempMapLight->getName()));
                break;

            case ServerNotification::exit:
            {
                running = false;
                //Ogre::LogManager::getSingleton().logMessage("Stopped server thread.", Ogre::LML_NORMAL);
                break;
            }

            default:
                cerr
                        << "\n\nERROR:  Unhandled ServerNotification type encoutered!\n\n";

                //TODO:  Remove me later - this is to force a core dump so I can debug why this happenened
                Creature * throwAsegfault = NULL;
                throwAsegfault->getPosition();

                exit(1);
                break;
        }

        // Decrement the number of outstanding references to things from the turn number the event was queued on.
        gameMap->threadUnlockForTurn(event->turnNumber);

        delete event;
        event = NULL;
    }

    // Return something to make the compiler happy
    return NULL;
}

/*! \brief The thread running  on the server which listens for messages from an individual, already connected, client.
 *
 * This thread recieves TCP packets one at a time from a connected client,
 * decodes them, and carries out requests for the client, returning any
 * results.  Since this is not the only thread which can send messages to the
 * client, a semaphore is used to control which thread talks to the client at
 * any given time.
 */
// THREAD - This function is meant to be called by pthread_create.
void *clientHandlerThread(void *p)
{
    Socket *curSock = ((CHTStruct*) p)->nSocket;
    ODFrameListener *frameListener = ((CHTStruct*) p)->nFrameListener;
    Player *curPlayer = NULL;
    delete (CHTStruct*) p;
    p = NULL;

    GameMap* gameMap = frameListener->getGameMap();
    std::string clientNick = "UNSET_CLIENT_NICKNAME";
    std::string clientCommand, arguments;
    std::string tempString, tempString2;

    while (true)
    {
        // Recieve a request from the client and store it in tempString
        int charsRead = curSock->recv(tempString);

        // If the client closed the connection
        if (charsRead <= 0)
        {
            frameListener->chatMessages.push_back(new ChatMessage(
                    "SERVER_INFORMATION: ", "Client disconnect: " + clientNick,
                    time(NULL)));
            break;
        }

        // If this command is not seperated by a colon into a
        // command and an argument then don't process it.  Send
        // the client an error message and move on to the next packet.
        unsigned int index = tempString.find(":");
        if (index == std::string::npos)
        {
            // Going back to the beginning of the loop effectively disregards this
            // message from the client.  This may cause problems if the command is
            // split up into many packets since the ":" might not be in the first packet.
            continue;
        }

        // Split the packet into a command and an argument
        parseCommand(tempString, clientCommand, arguments);
        //clientCommand = tempString.substr(1, index-1);
        //arguments = tempString.substr(index+1, tempString.size()-index-3);
        //cout << "\n\n\n" << clientCommand << "\n" << arguments;
        //cout.flush();

        if (clientCommand.compare("hello") == 0)
        {
            std::stringstream tempSS;
            frameListener->chatMessages.push_back(new ChatMessage(
                    "SERVER_INFORMATION: ", "Client connect with version: "
                            + arguments, time(NULL)));

            // Tell the client to give us their nickname and to clear their map
            sem_wait(&curSock->semaphore);
            curSock->send(formatCommand("picknick", ""));

            // Set the nickname that the client sends back, tempString2 is just used
            // to discard the command portion of the respone which should be "setnick"
            //TODO:  verify that this really is true
            curSock->recv(tempString);
            parseCommand(tempString, tempString2, clientNick);
            frameListener->chatMessages.push_back(new ChatMessage(
                    "SERVER_INFORMATION: ", "Client nick is: " + clientNick,
                    time(NULL)));

            // Create a player structure for the client
            //TODO:  negotiate and set a color
            curPlayer = new Player;
            curPlayer->setNick(clientNick);
            gameMap->addPlayer(curPlayer);

            curSock->send(formatCommand("newmap", ""));

            // Tell the player which seat it has
            tempSS.str("");
            tempSS << curPlayer->getSeat();
            curSock->send(formatCommand("addseat", tempSS.str()));

            tempSS.str("");
            tempSS << ODApplication::turnsPerSecond;
            curSock->send(formatCommand("turnsPerSecond", tempSS.str()));

            // Send over the information about the players in the game
            curSock->send(formatCommand("addplayer", gameMap->getLocalPlayer()->getNick()));
            for (unsigned int i = 0; i < gameMap->numPlayers(); ++i)
            {
                // Don't tell the client about its own player structure
                Player *tempPlayer = gameMap->getPlayer(i);
                if (curPlayer != tempPlayer && tempPlayer != NULL)
                {
                    tempSS.str("");
                    tempSS << tempPlayer->getSeat();
                    curSock->send(formatCommand("addseat", tempSS.str()));
                    // Throw away the ok response
                    curSock->recv(tempString);

                    curSock->send(formatCommand("addplayer", tempPlayer->getNick()));
                    // Throw away the ok response
                    curSock->recv(tempString);
                }
            }

            // Send over the map tiles from the current game map.
            //TODO: Only send the tiles which the client is supposed to see due to fog of war.
            TileMap_t::iterator itr = gameMap->firstTile();
            while (itr != gameMap->lastTile())
            {
                tempSS.str("");
                tempSS << itr->second;
                curSock->send(formatCommand("addtile", tempSS.str()));
                // Throw away the ok response
                curSock->recv(tempString);
                ++itr;
            }

            // Send over the map lights from the current game map.
            //TODO: Only send the maplights which the client is supposed to see due to the fog of war.
            for (unsigned int i = 0; i < gameMap->numMapLights(); ++i)
            {
                tempSS.str("");
                tempSS << gameMap->getMapLight(i);
                curSock->send(formatCommand("addmaplight", tempSS.str()));
                ++itr;
            }

            // Send over the rooms in use on the current game map
            //TODO: Only send the classes which the client is supposed to see due to fog of war.
            for (unsigned int i = 0; i < gameMap->numRooms(); ++i)
            {
                tempSS.str("");
                tempSS << gameMap->getRoom(i);
                curSock->send(formatCommand("addroom", tempSS.str()));
                // Throw away the ok response
                curSock->recv(tempString);
            }

            // Send over the class descriptions in use on the current game map
            //TODO: Only send the classes which the client is supposed to see due to fog of war.
            for (unsigned int i = 0; i < gameMap->numClassDescriptions(); ++i)
            {
                //NOTE: This code is duplicated in writeGameMapToFile defined in src/Functions.cpp
                // Changes to this code should be reflected in that code as well
                CreatureClass *tempClass = gameMap->getClassDescription(i);

                tempSS.str("");

                tempSS << tempClass;

                curSock->send(formatCommand("addclass", tempSS.str()));
                // Throw away the ok response
                //TODO:  Actually check this.
                curSock->recv(tempString);
            }

            // Send over the actual creatures in use on the current game map
            //TODO: Only send the creatures which the client is supposed to see due to fog of war.
            for (unsigned int i = 0; i < gameMap->numCreatures(); ++i)
            {
                Creature *tempCreature = gameMap->getCreature(i);

                tempSS.str("");

                tempSS << tempCreature;

                curSock->send(formatCommand("addcreature", tempSS.str()));
                // Throw away the ok response
                curSock->recv(tempString);
            }

            sem_post(&curSock->semaphore);
        }

        else if (clientCommand.compare("chat") == 0)
        {
            ChatMessage *newMessage = processChatMessage(arguments);

            // Send the message to all the connected clients
            for (unsigned int i = 0; i < frameListener->clientSockets.size(); ++i)
            {
                sem_wait(&frameListener->clientSockets[i]->semaphore);
                frameListener->clientSockets[i]->send(formatCommand("chat",
                        newMessage->getClientNick() + ":" + newMessage->getMessage()));
                sem_post(&frameListener->clientSockets[i]->semaphore);
            }

            // Put the message in our own queue
            frameListener->chatMessages.push_back(newMessage);
        }

        //NOTE:  This code is duplicated in clientSocketProcessor()
        else if (clientCommand.compare("creaturePickUp") == 0)
        {
            char array[255];

            std::stringstream tempSS;
            tempSS.str(arguments);

            tempSS.getline(array, sizeof(array), ':');
            std::string playerNick = array;
            tempSS.getline(array, sizeof(array));
            std::string creatureName = array;

            Player *tempPlayer = gameMap->getPlayer(playerNick);
            Creature *tempCreature = gameMap->getCreature(creatureName);

            if (tempPlayer != NULL && tempCreature != NULL)
            {
                tempPlayer->pickUpCreature(tempCreature);
            }
        }

        //NOTE:  This code is duplicated in clientSocketProcessor()
        else if (clientCommand.compare("creatureDrop") == 0)
        {
            char array[255];

            std::stringstream tempSS;
            tempSS.str(arguments);

            tempSS.getline(array, sizeof(array), ':');
            std::string playerNick = array;
            tempSS.getline(array, sizeof(array), ':');
            int tempX = atoi(array);
            tempSS.getline(array, sizeof(array));
            int tempY = atoi(array);

            Player *tempPlayer = gameMap->getPlayer(playerNick);
            Tile *tempTile = gameMap->getTile(tempX, tempY);

            if (tempPlayer != NULL && tempTile != NULL)
            {
                tempPlayer->dropCreature(tempTile);
            }
        }

        else if (clientCommand.compare("markTile") == 0)
        {
            char array[255];
            std::stringstream tempSS;
            tempSS.str(arguments);

            tempSS.getline(array, sizeof(array), ':');
            int tempX = atoi(array);
            tempSS.getline(array, sizeof(array), ':');
            int tempY = atoi(array);
            tempSS.getline(array, sizeof(array));
            std::string flagName = array;

            Tile *tempTile = gameMap->getTile(tempX, tempY);
            if (tempTile != NULL)
            {
                Player *tempPlayer = gameMap->getPlayer(clientNick);
                if (tempPlayer != NULL)
                {
                    bool flag;
                    flagName.compare("true") == 0 ? flag = true : flag = false;
                    tempTile->setMarkedForDigging(flag, tempPlayer);
                }
            }
        }

        else if (clientCommand.compare("ok") == 0)
        {
            cout << "\nIgnoring an ak message from a client: " << arguments;
        }

        else
        {
            cerr
                    << "\n\nERROR:  Unhandled command recieved from client:\nCommand:  ";
            cerr << clientCommand << "\nArguments:  " << arguments << "\n\n";
            exit(1);
        }

        /*if(frameListener->getThreadStopRequested())
        {
            //TODO - log
            break;
        }*/
    }

    // Return something to make the compiler happy
    return NULL;
}

void sendToAllClients(ODFrameListener *frameListener, std::string str)
{
    for (unsigned int i = 0; i < frameListener->clientSockets.size(); ++i)
    {
        sem_wait(&frameListener->clientSockets[i]->semaphore);
        frameListener->clientSockets[i]->send(str);
        sem_post(&frameListener->clientSockets[i]->semaphore);
    }
}
