/*
Copyright (c) 2013 Vladimir Jimenez, Ned Anderson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Author:
Vlad Jimenez (allejo)
Ned Anderson (mdskpr)

Description:
Planet MoFo's Cup

Slash Commands:
/cup
/rank

License:
BSD

Version:
0.7
*/

#include <iostream>
#include <fstream>
#include <queue>
#include <sqlite3.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "bzfsAPI.h"

//keep track of the people we need to send messages to
struct messagesToSend
{
    int sendTo;
};

std::queue<messagesToSend> messageQueue; //all the messages we need to send
messagesToSend currentMessage; //the current method we're dealing with

int leaderBoard = 1;

int showCupLeaderBoard(void *a_param, int argc, char **argv, char **column)
{
    if (!messageQueue.empty())
    {
        bz_sendTextMessagef(BZ_SERVER, currentMessage.sendTo, "#%i     %i        %s", leaderBoard, atoi(argv[2]), argv[4]);
        leaderBoard++;
    }
    else
    {
        bz_debugMessage(2, "DEBUG :: MoFo Cup :: There is no one to send this '/cup' query to!");
    }

    return 0;
}

int showRankToPlayer(void *a_param, int argc, char **argv, char **column)
{
    currentMessage = messageQueue.front();

    bz_debugMessagef(3, "DEBUG :: MoFo Cup :: Sending /rank data to player id %i", currentMessage.sendTo);

    if (!messageQueue.empty())
    {
        bz_sendTextMessagef(BZ_SERVER, currentMessage.sendTo, "You're currently #%i in the MoFo Cup", atoi(argv[0]));

        messageQueue.pop(); //remove this message from the queue
    }
    else
    {
        bz_debugMessage(2, "DEBUG :: MoFo Cup :: There is no one to send this '/rank' query to!");
    }

    return 0;
}

int announceCaptureEvent(void *a_param, int argc, char **argv, char **column)
{
    //still to do
}

class mofocup : public bz_Plugin, public bz_CustomSlashCommandHandler
{
public:
    sqlite3* db; //sqlite database we'll be using
    std::string dbfilename; //the path to the database

    virtual const char* Name (){return "MoFo Cup";}
    virtual void Init(const char* /*commandLine*/);
    virtual void Cleanup(void);

    virtual void Event(bz_EventData *eventData);
    virtual bool SlashCommand(int playerID, bz_ApiString command, bz_ApiString message, bz_APIStringList *params);

    virtual void doQuery(std::string query);
    virtual std::string convertToString(int myInt);

    //we're storing the time people play so we can rank players based on how quick they make as many caps
    struct playingTimeStructure
    {
        std::string bzid;
        double joinTime;
    };
    std::vector<playingTimeStructure> playingTime;
};

BZ_PLUGIN(mofocup);

void mofocup::Init(const char* commandLine)
{
    bz_registerCustomSlashCommand("cup", this); //register the /cup command
    bz_registerCustomSlashCommand("rank", this); //register the /rank command

    if(commandLine == NULL || std::string(commandLine).empty()) //no database provided, unloadplugin ourselves
    {
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: Please provide a filename for the database");
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: -loadplugin /path/to/mofocup.so,/path/to/database.db");
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: Unloading MoFoCup plugin...");
        bz_unloadPlugin(Name());
    }
    else
    {
        dbfilename = std::string(commandLine);
        bz_debugMessagef(0, "DEBUG :: MoFo Cup :: Using the following database: %s", dbfilename.c_str());
        sqlite3_open(dbfilename.c_str(),&db);

        if (db == 0) //we couldn't read the database provided
        {
            bz_debugMessagef(0, "DEBUG :: MoFo Cup :: Error! Could not connect to: %s", dbfilename.c_str());
            bz_debugMessage(0, "DEBUG :: MoFo Cup :: Unloading MoFoCup plugin...");
            bz_unloadPlugin(Name());
        }

        if (db != 0) //if the database is empty, let's create the tables needed
        {
            doQuery("CREATE TABLE Cups(CupID INTEGER, ServerID TEXT, StartTime TEXT, EndTime Text, CupType Text, primary key (CupID))");
            doQuery("CREATE TABLE Captures(BZID INTEGER, CupID INTEGER, Counter INTEGER default (0), primary key(BZID,CupID))");
        }

        if (!Register(bz_ePlayerDieEvent) || !Register(bz_eCaptureEvent) || !Register(bz_ePlayerPartEvent) || !Register(bz_ePlayerJoinEvent)) //unload the plugin if any events fail to register
        {
            bz_debugMessage(0, "DEBUG :: MoFo Cup :: A BZFS event failed to load.");
            bz_debugMessage(0, "DEBUG :: MoFo Cup :: Unloading MoFoCup plugin...");
            bz_unloadPlugin(Name());
        }
    }

    bz_debugMessage(4, "DEBUG :: MoFo Cup :: Successfully loaded and database connection ready.");
}

void mofocup::Cleanup()
{
    Flush();
    bz_removeCustomSlashCommand("cup");
    bz_removeCustomSlashCommand("rank");

    if (db != NULL) //close the database connection since we won't need it
      sqlite3_close(db);

    bz_debugMessage(4, "DEBUG :: MoFo Cup :: Successfully unloaded and database connection closed.");
}

void mofocup::Event(bz_EventData* eventData)
{
    switch (eventData->eventType)
    {
        case bz_eCaptureEvent:
        {
            /*

                MoFo Cup :: Capping Tournament
                ------------------------------

                Don't ask about the giant query, it works. We're going to
                be incrementing the amount of caps a player has by one
                every time he or she caps. In the event of a tie of caps,
                we are going to determine the winner by who has capped in
                the least amount of seconds played.

                Notes
                -----
                We're using L4m3r's forumla to calculate how many points
                a capture was worth.

                    8 * (numberOfPlayersOnCappedTeam - numberOfPlayersOnCappingTeam) + 3 * (numberOfPlayersOnCappedTeam)

                Here's the formula we will be using to calculate a player's
                overall rating in the capturing cup.

                    (Total of Cap Points) / (Total Seconds Played / 86400)

            */

            bz_CTFCaptureEventData_V1* ctfdata = (bz_CTFCaptureEventData_V1*)eventData;

            int points, playingTime, newRank;
            int bonusPoints = 8 * (bz_getTeamCount(ctfdata->teamCapped) - bz_getTeamCount(ctfdata->teamCapping)) + 3 * bz_getTeamCount(ctfdata->teamCapped);
            std::string bzid = std::string(bz_getPlayerByIndex(ctfdata->playerCapping)->bzID.c_str()); //we're storing the capper's bzid
            sqlite3_stmt *statement;

            if(sqlite3_prepare_v2(db, "SELECT `Points`, `PlayingTime` FROM `Captures` WHERE `BZID` = ?", -1, &statement, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(statement, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
                int cols = sqlite3_column_count(statement), result = 0;

                result = sqlite3_step(statement);
                points = atoi((char*)sqlite3_column_text(statement, 0));
                playingTime = atoi((char*)sqlite3_column_text(statement, 1));

                sqlite3_finalize(statement);
            }

            newRank = (points)/(playingTime/86400);

            std::string query = ""
            "INSERT OR REPLACE INTO `Captures` (BZID, CupID, Callsign, Points, Rating, PlayingTime) "
            "VALUES ('" + bzid + "', "
            "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
            "(SELECT `Callsign` FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + bzid + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`)), "
            "(SELECT COALESCE((SELECT `Points` + '" + convertToString(bonusPoints) + "' FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + bzid + "' AND `Captures`.`CupID` = `Cups`.`CupID` AND `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), '" + convertToString(bonusPoints) + "')), "
            newRank + ", "
            "(SELECT `PlayingTime` FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + bzid + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`)";

            bz_debugMessage(2, "DEBUG :: MoFo Cup :: Executing following SQL query...");
            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", query.c_str());

            char* db_err = 0;
            int ret = sqlite3_exec(db, query.c_str(), announceCaptureEvent, 0, &db_err);

            if (db_err != 0)
            {
                bz_debugMessage(2, "DEBUG :: MoFo Cup :: SQL ERROR!");
                bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", db_err);
            }
        }
        break;

        case bz_ePlayerDieEvent:
        {
            bz_PlayerDieEventData_V1* diedata = (bz_PlayerDieEventData_V1*)eventData;

            //Still to do
        }
        break;

        case bz_ePlayerJoinEvent:
        {
            /*
                MoFo Cup :: Notes
                -----------------

                Because of the possibility of a tie can occur, we are going
                to be keeping track of the amount of time a player has played
                and we will determine the winner by who has acheived the most
                in the least mount of time played. We will be keeping track
                of the amount of seconds played. Each time a player joins, we
                will store the time.

            */

            bz_PlayerJoinPartEventData_V1* joindata = (bz_PlayerJoinPartEventData_V1*)eventData;

            if(atoi(joindata->record->bzID.c_str()) > 0 && joindata->record->team != eObservers)
            {
                playingTimeStructure newPlayingTime;

                newPlayingTime.bzid = joindata->record->bzID.c_str();
                newPlayingTime.joinTime = bz_getCurrentTime();

                playingTime.push_back(newPlayingTime);
            }
        }
        break;

        case bz_ePlayerPartEvent:
        {
            /*
                MoFo Cup :: Notes
                -----------------

                Going hand in hand with the bz_ePlayerJoinEvent, we are going
                to be checking the time a player has joined and subtracting
                the amount of seconds played when the player leaves. We will
                then add this time to the database and we will know the total
                amount of time played.

            */

            bz_PlayerJoinPartEventData_V1* partdata = (bz_PlayerJoinPartEventData_V1*)eventData;

            if (playingTime.size() > 0)
            {
                for (unsigned int i = 0; i < playingTime.size(); i++)
                {
                    if (playingTime.at(i).bzid == partdata->record->bzID.c_str() && partdata->record->team != eObservers)
                    {
                        std::string myBZID = partdata->record->bzID.c_str();
                        std::string callsign = partdata->record->callsign.c_str();
                        int timePlayed = bz_getCurrentTime() - playingTime.at(i).joinTime;
                        std::string updatePlayingTime = ""
                        "INSERT OR REPLACE INTO `Captures` (BZID, CupID, Counter, PlayingTime, Callsign) "
                        "VALUES ('" + myBZID + "', "
                        "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`), "
                        "(SELECT `Points` FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + myBZID + "' AND `Captures`.`CupID` = `Cups`.`CupID` AND `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`), "
                        "(SELECT COALESCE((SELECT `PlayingTime` + " + convertToString(timePlayed) + " FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + myBZID + "' AND `Captures`.`CupID` = `Cups`.`CupID` AND `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`), '1')), "
                        "'" + callsign + "')";

                        doQuery(updatePlayingTime);
                        playingTime.erase(playingTime.begin() + i, playingTime.begin() + i + 1);
                    }
                }
            }
        }
        break;

        default:
        break;
    }
}

bool mofocup::SlashCommand(int playerID, bz_ApiString command, bz_ApiString message, bz_APIStringList *params)
{
    if(command == "cup")
    {
        messagesToSend newTask; //we got a new message to send
        newTask.sendTo = playerID; //let's get their player id
        messageQueue.push(newTask); //push it to the queue

        bz_debugMessagef(3, "DEBUG :: MoFo Cup :: Player ID %i was added to the message queue for /cup data.", newTask.sendTo);

        char* db_err = 0;
        std::string query = "SELECT * FROM `Captures` WHERE `CupID` = (SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`) ORDER BY `Points` DESC, `PlayingTime` ASC LIMIT 10";

        bz_debugMessage(3, "DEBUG :: MoFo Cup :: The /cup command has been executed");
        bz_debugMessage(2, "DEBUG :: MoFo Cup :: Executing following SQL query...");
        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", query.c_str());

        currentMessage = messageQueue.front();

        bz_debugMessagef(3, "DEBUG :: MoFo Cup :: Sending /rank data to player id %i", currentMessage.sendTo);

        bz_sendTextMessagef(BZ_SERVER, currentMessage.sendTo, "Planet MoFo Cup || Top 10");
        bz_sendTextMessagef(BZ_SERVER, currentMessage.sendTo, "-------------------------");
        bz_sendTextMessagef(BZ_SERVER, currentMessage.sendTo, "      Caps     Callsign");

        int ret = sqlite3_exec(db, query.c_str(), showCupLeaderBoard, 0, &db_err);

        leaderBoard = 1;
        messageQueue.pop(); //remove this message from the queue

        if (db_err != 0)
        {
            bz_debugMessage(2, "DEBUG :: MoFo Cup :: SQL ERROR!");
            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", db_err);
        }

    }
    else if(command == "rank")
    {
        messagesToSend newTask; //we got a new message to send
        newTask.sendTo = playerID; //let's get their player id
        messageQueue.push(newTask); //push it to the queue

        bz_debugMessagef(3, "DEBUG :: MoFo Cup :: Player ID %i was added to the message queue for /rank data.", newTask.sendTo);

        char* db_err = 0;
        std::string query = "SELECT (SELECT COUNT(*) FROM `captures` AS c2 WHERE c2.Counter > c1.Counter) + 1 AS row_Num FROM `Captures` AS c1 WHERE `BZID` = '" + std::string(bz_getPlayerByIndex(playerID)->bzID.c_str()) + "' ORDER BY Counter DESC LIMIT 1";

        bz_debugMessage(3, "DEBUG :: MoFo Cup :: The /rank command has been executed");
        bz_debugMessage(2, "DEBUG :: MoFo Cup :: Executing following SQL query...");
        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", query.c_str());

        int ret = sqlite3_exec(db, query.c_str(), showRankToPlayer, 0, &db_err);

        if (db_err != 0)
        {
            bz_debugMessage(2, "DEBUG :: MoFo Cup :: SQL ERROR!");
            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", db_err);
        }
    }
}

std::string mofocup::convertToString(int myInt)
{
    std::string myString;
    std::stringstream string;
    string << myInt; //Use an stringstream to pass an int through a string
    myString = string.str();

    return myString;
}

void mofocup::doQuery(std::string query)
{
    bz_debugMessage(2, "DEBUG :: MoFo Cup :: Executing following SQL query...");
    bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", query.c_str());

    char* db_err = 0;
    int ret = sqlite3_exec(db, query.c_str(), NULL, 0, &db_err);

    if (db_err != 0)
    {
        bz_debugMessage(2, "DEBUG :: MoFo Cup :: SQL ERROR!");
        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", db_err);
    }
}
