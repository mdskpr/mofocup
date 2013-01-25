/*
Copyright (c) 2012 Vladimir Jimenez, Ned Anderson
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

int showCupLeaderBoard(void *a_param, int argc, char **argv, char **column)
{
    if (!messageQueue.empty())
    {
        bz_sendTextMessagef(BZ_SERVER, currentMessage.sendTo, "Planet MoFo Cup || Top 10");
        bz_sendTextMessagef(BZ_SERVER, currentMessage.sendTo, "-------------------------");

        //i needs a for loop

        messageQueue.pop(); //remove this message from the queue
    }
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
    virtual void incrementCounter(std::string bzid, std::string cuptype, std::string incrementBy);
    virtual std::string convertToString(int myInt);
    virtual int determineRank(std::string bzID);

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
            doQuery("CREATE TABLE Cups(CupID INTEGER, ServerID TEXT, StartTime TEXT, EndTime Text, CupType Text, primary key (CupID) )");
            doQuery("CREATE TABLE Captures(BZID INTEGER, CupID INTEGER, Counter INTEGER default (0), primary key(BZID,CupID) )");
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
        case bz_eCaptureEvent: // A flag is captured
        {
            bz_CTFCaptureEventData_V1* ctfdata = (bz_CTFCaptureEventData_V1*)eventData;
            bz_BasePlayerRecord *pr = bz_getPlayerByIndex(ctfdata->playerCapping);

            std::string capturerid = pr->bzID.c_str();
            incrementCounter(capturerid, "flag_capture", "1");

            bz_freePlayerRecord(pr);

            /*
            Uhh.... Not sure what to do yet

            int pre_rank;
            int post_rank;
            bz_BasePlayerRecord *pr = bz_getPlayerByIndex(ctfdata->playerCapping);
            pre_rank = determineRank(pr->bzID.c_str());

            std::string capturerid = pr->bzID.c_str();
            incrementCounter(capturerid, "flag_capture", "1");

    	    post_rank = determineRank(pr->bzID.c_str());
    	    bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, eActionMessage, "Player: %s captured the flag! Earning 1 point.", pr->callsign.c_str());

    	    if(post_rank > pre_rank)
            {
                bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, eActionMessage, "Player: %s is now rank %i", pr->callsign.c_str(), post_rank);
    	    }

            bz_freePlayerRecord(pr);
            */
        }
        break;

        case bz_ePlayerDieEvent: // A player dies
        {
            bz_PlayerDieEventData_V1* diedata = (bz_PlayerDieEventData_V1*)eventData;

            // Members for bz_PlayerDieEventData_V1:

            // int playerID: The victim's ID
            // bz_eTeamType team: The victim's team
            // int killerID: The killer's ID
            // bz_eTeamType killerTeam: The killer's team
            // bz_ApiString flagKilledWith: The flag that the killer shot the victim with
            // int shotID: The shot ID of the bullet that killed the victim
            // int driverID: ID of the physics driver that killed the player, where applicable
            // bz_PlayerUpdateState state: The victim's state at the time of death
            // double eventTime: The game time (in seconds)

        }
        break;

        case bz_ePlayerJoinEvent: // A player joins
        {
            bz_PlayerJoinPartEventData_V1* joindata = (bz_PlayerJoinPartEventData_V1*)eventData;

            if(atoi(joindata->record->bzID.c_str()) > 0)
            {
                playingTimeStructure newPlayingTime;

                newPlayingTime.bzid = joindata->record->bzID.c_str();
                newPlayingTime.joinTime = bz_getCurrentTime();

                playingTime.push_back(newPlayingTime);
            }
        }
        break;

        case bz_ePlayerPartEvent: // A player parts
        {
            bz_PlayerJoinPartEventData_V1* partdata = (bz_PlayerJoinPartEventData_V1*)eventData;

            if (playingTime.size() > 0)
            {
                for (unsigned int i = 0; i < playingTime.size(); i++)
                {
                    if (playingTime.at(i).bzid == partdata->record->bzID.c_str())
                    {
                        std::string myBZID = partdata->record->bzID.c_str();
                        std::string callsign = partdata->record->callsign.c_str();
                        int timePlayed = bz_getCurrentTime() - playingTime.at(i).joinTime;
                        std::string updatePlayingTime = "INSERT OR REPLACE INTO `Captures` (BZID, CupID, Counter, PlayingTime, Callsign) ";
                        updatePlayingTime += "VALUES ('" + myBZID + "', ";
                        updatePlayingTime += "'(SELECT `Counter` FROM `Captures` WHERE `Captures`.`BZID` = '" + myBZID + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`)', ";
                        updatePlayingTime += "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), ";
                        updatePlayingTime += "(SELECT COALESCE((SELECT `PlayingTime` + " + convertToString(timePlayed) + " FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + myBZID + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 1)), ";
                        updatePlayingTime += callsign + ")";

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
        messagesToSend newTask;
        newTask.sendTo = playerID;

        char* db_err = 0;
        std::string query = "SELECT * FROM `Captures` WHERE `CupID` = (SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`) ORDER BY `Counter` DESC, `PlayingTime` ASC LIMIT 10";
        int ret = sqlite3_exec(db, query.c_str(), showCupLeaderBoard, 0, &db_err);

        messageQueue.push(newTask);
    }
    else if(command == "rank")
    {
        //Handle rank command.
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

/*int mofocup::determineRank(std::string bzID)
{
    char* db_err = 0;
    std::string query = "SELECT (SELECT COUNT(*) FROM `captures` AS c2 WHERE c2.Counter > c1.Counter) + 1 AS row_Num FROM `Captures` AS c1 WHERE `BZID` = '" + bzID + "' ORDER BY Counter DESC";
    int ret = sqlite3_exec(db, query.c_str(), showCupLeaderBoard, 0, &db_err);
}*/

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

void mofocup::incrementCounter(std::string bzid, std::string cuptype, std::string incrementBy)
{
    char* db_err = 0;

    //don't ask about the query...
    std::string query = "INSERT OR REPLACE INTO `Captures` (BZID, CupID, Counter, PlayingTime, Callsign) ";
    query += "VALUES ('" + bzid + "', ";
    query += "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), ";
    query += "(SELECT COALESCE((SELECT `Counter` + 1 FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + bzid + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 1)))";
    query += "(SELECT `PlayingTime` FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + bzid + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), ";
    query += "(SELECT `Callsign` FROM `Captures`, `Cups` WHERE `Captures`.`BZID` = '" + bzid + "' AND `Captures`.`CupID` = `Cups`.`CupID` and `ServerID` = '" + std::string(bz_getPublicAddr().c_str()) + "' AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`))";

    doQuery(query);
}