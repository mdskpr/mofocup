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

/*
Formulas

[23:00:37] <I_Died_Once> (Total of Cap Points) / (Total Seconds Played / 86400) = CTF Cup
[23:00:37] <I_Died_Once> (Sum of all bounties) / (Total Seconds Played / 86400) = Bounty Cup
[23:00:37] <I_Died_Once> (Sum of all geno points) / (Total Seconds Played / 86400) = Geno Cup
[23:00:37] <I_Died_Once> (Number of Kills) / (Total Seconds Played / 86400) = Kills Cup
*/

#include <iostream>
#include <fstream>
#include <sqlite3.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "bzfsAPI.h"

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

    virtual void addCurrentPlayingTime(std::string bzid, std::string callsign);
    virtual void doQuery(std::string query);
    virtual std::string convertToString(int myInt);
    virtual void trackNewPlayingTime(std::string bzid);
    virtual void updatePlayerRatio(std::string bzid);

    //we're storing the time people play so we can rank players based on how quick they make as many caps
    struct playingTimeStructure
    {
        std::string bzid;
        double joinTime;
    };
    std::vector<playingTimeStructure> playingTime;

    double lastDatabaseUpdate;
};

BZ_PLUGIN(mofocup);

//Initialize all the available cups
std::string cups[] = {"bounty", "ctf", "geno", "kills"};

void mofocup::Init(const char* commandLine)
{
    bz_registerCustomSlashCommand("cup", this); //register the /cup command
    bz_registerCustomSlashCommand("rank", this); //register the /rank command

    if (commandLine == NULL || std::string(commandLine).empty()) //no database provided, unloadplugin ourselves
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

        if (!Register(bz_ePlayerDieEvent) || !Register(bz_eCaptureEvent) || !Register(bz_ePlayerPartEvent) || !Register(bz_ePlayerJoinEvent) || !Register(bz_ePlayerPausedEvent) || !Register(bz_eTickEvent)) //unload the plugin if any events fail to register
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

            if (std::string(convertToString(ctfdata->playerCapping)).empty()) //ignore the cap if it's an unregistered player
                return;

            //update playing time of the capper to accurately calculate the total points
            addCurrentPlayingTime(bz_getPlayerByIndex(ctfdata->playerCapping)->bzID.c_str(), bz_getPlayerByIndex(ctfdata->playerCapping)->callsign.c_str());
            trackNewPlayingTime(bz_getPlayerByIndex(ctfdata->playerCapping)->bzID.c_str());

            //initialize some stuff
            float newRankDecimal;
            int points, playingTime, newRank, oldRank;
            int bonusPoints = 8 * (bz_getTeamCount(ctfdata->teamCapped) - bz_getTeamCount(ctfdata->teamCapping)) + 3 * bz_getTeamCount(ctfdata->teamCapped); //calculate the amount of bonus points
            std::string bzid = std::string(bz_getPlayerByIndex(ctfdata->playerCapping)->bzID.c_str()); //we're storing the capper's bzid
            sqlite3_stmt *currentStats;

            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has captured the flag earning %i points towards the MoFo Cup", bz_getPlayerByIndex(ctfdata->playerCapping)->callsign.c_str(), bz_getPlayerByIndex(ctfdata->playerCapping)->bzID.c_str(), bonusPoints);

            if (sqlite3_prepare_v2(db, "SELECT `Points`, `PlayingTime`, `Rating` FROM `CTFCup` WHERE `BZID` = ?", -1, &currentStats, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(currentStats, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
                int result = sqlite3_step(currentStats);
                points = atoi((char*)sqlite3_column_text(currentStats, 0));
                playingTime = atoi((char*)sqlite3_column_text(currentStats, 1));
                oldRank = atoi((char*)sqlite3_column_text(currentStats, 2));

                sqlite3_finalize(currentStats);
            }

            newRankDecimal = (float)points/(float)((float)playingTime/86400.0);
            newRank = int(newRankDecimal);

            bz_debugMessagef(3, "DEBUG :: MoFo Cup :: %s (%s)", bz_getPlayerByIndex(ctfdata->playerCapping)->callsign.c_str(), bz_getPlayerByIndex(ctfdata->playerCapping)->bzID.c_str());
            bz_debugMessagef(3, "DEBUG :: MoFo Cup :: -------------------------");
            bz_debugMessagef(3, "DEBUG :: MoFo Cup :: Points change: %i -> %i", points, points+bonusPoints);
            bz_debugMessagef(3, "DEBUG :: MoFo Cup :: Ratio change: %i -> %i", oldRank, newRank);
            bz_debugMessagef(3, "DEBUG :: MoFo Cup :: Playing time: %i minutes", int(playingTime/60));

            std::string query = ""
            "INSERT OR REPLACE INTO `CTFCup` (BZID, CupID, Callsign, Points, Rating, PlayingTime) "
            "VALUES (?, "
            "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
            "(SELECT `Callsign` FROM `CTFCup`, `Cups` WHERE `CTFCup`.`BZID` = ? AND `CTFCup`.`CupID` = `Cups`.`CupID` and `ServerID` = ? AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
            "(SELECT COALESCE((SELECT `Points` + ? FROM `CTFCup`, `Cups` WHERE `CTFCup`.`BZID` = ? AND `CTFCup`.`CupID` = `Cups`.`CupID` AND `ServerID` = ? AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), ?)), "
            "?, "
            "(SELECT `PlayingTime` FROM `CTFCup`, `Cups` WHERE `CTFCup`.`BZID` = ? AND `CTFCup`.`CupID` = `Cups`.`CupID` and `ServerID` = ? AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`))";

            sqlite3_stmt *newStats;

            if (sqlite3_prepare_v2(db, query.c_str(), -1, &newStats, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(newStats, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 2, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 3, bzid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 4, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 5, convertToString(bonusPoints).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 6, bzid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 7, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 8, convertToString(bonusPoints).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 9, convertToString(newRank).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 10, bzid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(newStats, 11, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);

                int result = sqlite3_step(newStats);
                sqlite3_finalize(newStats);
            }
            else //could not prepare the statement
            {
                bz_debugMessagef(2, "Error #%i: %s", sqlite3_errcode(db), sqlite3_errmsg(db));
            }
        }
        break;

        case bz_ePlayerDieEvent:
        {
            bz_PlayerDieEventData_V1* diedata = (bz_PlayerDieEventData_V1*)eventData;

            if (std::string(bz_getPlayerByIndex(diedata->killerID)->bzID.c_str()).empty())
                return;
                
            /*
                MoFo Cup :: Kills Cup
                ---------------------
                
                Keep track of all the kills a player makes
            */
            
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

            if (std::string(joindata->record->bzID.c_str()).empty() || joindata->record->team == eObservers) //don't do anything if the player is an observer or is not registered
                return;

            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has started to play, now recording playing time.", joindata->record->callsign.c_str(), joindata->record->bzID.c_str());
            trackNewPlayingTime(joindata->record->bzID.c_str());
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

            if (std::string(partdata->record->bzID.c_str()).empty() || partdata->record->team == eObservers) //don't do anything if the player is an observer or is not registered
                return;

            addCurrentPlayingTime(partdata->record->bzID.c_str(), partdata->record->callsign.c_str()); //they left, let's add their playing time to the database
            updatePlayerRatio(partdata->record->bzID.c_str());
        }
        break;

        case bz_ePlayerPausedEvent:
        {
            /*
                MoFo Cup :: Notes
                -----------------

                In order not to penalize players for pausing, we will not count
                the amount of time they have played when paused.

            */

            bz_PlayerPausedEventData_V1* pausedata = (bz_PlayerPausedEventData_V1*)eventData;

            if (std::string(bz_getPlayerByIndex(pausedata->playerID)->bzID.c_str()).empty()) //don't bother if the player isn't registered
                return;

            if (pausedata->pause) //when a player pauses, we add their current playing time to the database
                addCurrentPlayingTime(bz_getPlayerByIndex(pausedata->playerID)->bzID.c_str(), bz_getPlayerByIndex(pausedata->playerID)->callsign.c_str());
            else //start tracking a player's playing time when they have unpaused
                trackNewPlayingTime(bz_getPlayerByIndex(pausedata->playerID)->bzID.c_str());
        }
        break;

        case bz_eTickEvent:
        {
            if (lastDatabaseUpdate + 300 < bz_getCurrentTime())
            {
                lastDatabaseUpdate = bz_getCurrentTime();

                bz_APIIntList *playerList = bz_newIntList();
                bz_getPlayerIndexList(playerList);

                for (unsigned int i = 0; i < playerList->size(); i++)
                {
                    if (std::string(bz_getPlayerByIndex(playerList->get(i))->bzID.c_str()).empty())
                        continue;

                    addCurrentPlayingTime(bz_getPlayerByIndex(playerList->get(i))->bzID.c_str(), bz_getPlayerByIndex(playerList->get(i))->callsign.c_str());
                    updatePlayerRatio(bz_getPlayerByIndex(playerList->get(i))->bzID.c_str());
                    trackNewPlayingTime(bz_getPlayerByIndex(playerList->get(i))->bzID.c_str());
                }

                bz_deleteIntList(playerList);
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
        if (strcmp(params->get(0).c_str(), "ctf") == 0)
        {
            sqlite3_stmt *statement;

            if (sqlite3_prepare_v2(db, "SELECT `PlayingTime`.`Callsign`, `CTFCup`.`Rating` FROM `CTFCup`, `PlayingTime` WHERE `PlayingTime`.`BZID` = `CTFCup`.`BZID` AND `CTFCup`.`CupID` = (SELECT `Cups`.`CupID` FROM `Cups` WHERE `Cups`.`ServerID` = ? AND `Cups`.`CupType` = 'capture' AND strftime('%s','now') < `Cups`.`EndTime` AND strftime('%s','now') > `Cups`.`StartTime`) ORDER BY `CTFCup`.`Rating` DESC, `PlayingTime`.`PlayingTime` ASC LIMIT 5", -1, &statement, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(statement, 1, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
                int cols = sqlite3_column_count(statement), result = 0, counter = 1;

                bz_sendTextMessage(BZ_SERVER, playerID, "Planet MoFo CTF Cup");
                bz_sendTextMessage(BZ_SERVER, playerID, "--------------------");
                bz_sendTextMessage(BZ_SERVER, playerID, "        Callsign                    Points");

                while (true)
                {
                    result = sqlite3_step(statement);

                    if (result == SQLITE_ROW)
                    {
                        std::string place = "#" + convertToString(counter);
                        std::string playerCallsign = (char*)sqlite3_column_text(statement, 0);
                        std::string playerRatio = (char*)sqlite3_column_text(statement, 1);

                        if (playerCallsign.length() >= 26) playerCallsign = playerCallsign.substr(0, 26);
                        while (place.length() < 8) place += " ";
                        while (playerCallsign.length() < 28) playerCallsign += " ";
                        while (playerRatio.length() <    6) playerRatio += " ";

                        bz_sendTextMessage(BZ_SERVER, playerID, std::string(place + playerCallsign + playerRatio).c_str());
                    }
                    else
                        break;

                    counter++;
                }

                sqlite3_finalize(statement);
            }

            if (std::string(bz_getPlayerByIndex(playerID)->bzID.c_str()).empty())
                return true;

            bz_sendTextMessage(BZ_SERVER, playerID, " ");

            if (sqlite3_prepare_v2(db, "SELECT `Rating`, (SELECT COUNT(*) FROM `CTFCup` AS c2 WHERE c2.Rating > c1.Rating) + 1 AS row_Num FROM `CTFCup` AS c1 WHERE `BZID` = ?", -1, &statement, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(statement, 1, std::string(bz_getPlayerByIndex(playerID)->bzID.c_str()).c_str(), -1, SQLITE_TRANSIENT);
                int cols = sqlite3_column_count(statement), result = 0;

                while (true)
                {
                    result = sqlite3_step(statement);

                    if (result == SQLITE_ROW)
                    {
                        std::string myRatio = (char*)sqlite3_column_text(statement, 0);
                        std::string myPosition = std::string("#") + (char*)sqlite3_column_text(statement, 1);
                        std::string myCallsign = bz_getPlayerByIndex(playerID)->callsign.c_str();

                        if (myCallsign.length() >= 26) myCallsign = myCallsign.substr(0, 26);
                        while (myPosition.length() < 8) myPosition += " ";
                        while (myCallsign.length() < 28) myCallsign += " ";
                        while (myRatio.length() < 6) myRatio += " ";

                        bz_sendTextMessage(BZ_SERVER, playerID, std::string(myPosition + myCallsign + myRatio).c_str());
                    }
                    else
                        break;
                }

                sqlite3_finalize(statement);
            }
        }
        else
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "Usage: /cup <bounty | ctf | geno | kills>");
            bz_sendTextMessage(BZ_SERVER, playerID, "See '/help cup' for more information regarding the MoFup Cup.");
        }

        return true;
    }
    else if(command == "rank")
    {
        if (std::string(bz_getPlayerByIndex(playerID)->bzID.c_str()).empty())
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "You are not a registered BZFlag player, please register at 'http://forums.bzflag.org' in order to join the MoFo Cup.");
            return true;
        }

        sqlite3_stmt *statement;

        if (strcmp(params->get(0).c_str(), "") != 0)
        {
            if (sqlite3_prepare_v2(db, "SELECT `Rating`, (SELECT COUNT(*) FROM `CTFCup` AS c2 WHERE c2.Rating > c1.Rating) + 1 AS row_Num FROM `CTFCup` AS c1 WHERE `Callsign` = ?", -1, &statement, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(statement, 1, params->get(0).c_str(), -1, SQLITE_TRANSIENT);
                int result = sqlite3_step(statement);

                if (result == SQLITE_ROW)
                {
                    std::string playerPoints = (char*)sqlite3_column_text(statement, 0);
                    std::string playerRatio = (char*)sqlite3_column_text(statement, 1);
                    bz_sendTextMessagef(BZ_SERVER, playerID, "%s is currently #%s in the CTF Cup with a CTF score of %s", params->get(0).c_str(), playerRatio.c_str(), playerPoints.c_str());
                }
                else
                    bz_sendTextMessagef(BZ_SERVER, playerID, "%s is not part of the current MoFo Cup.", params->get(0).c_str());

                sqlite3_finalize(statement);
            }
        }
        else
        {
            if (sqlite3_prepare_v2(db, "SELECT `Rating`, (SELECT COUNT(*) FROM `CTFCup` AS c2 WHERE c2.Rating > c1.Rating) + 1 AS row_Num FROM `CTFCup` AS c1 WHERE `BZID` = ?", -1, &statement, 0) == SQLITE_OK)
            {
                sqlite3_bind_text(statement, 1, std::string(bz_getPlayerByIndex(playerID)->bzID.c_str()).c_str(), -1, SQLITE_TRANSIENT);
                int result = sqlite3_step(statement);
                std::string playerPoints = (char*)sqlite3_column_text(statement, 0);
                std::string playerRatio = (char*)sqlite3_column_text(statement, 1);
                bz_sendTextMessagef(BZ_SERVER, playerID, "You are currently #%s in CTF Cup with a CTF score of %s", playerRatio.c_str(), playerPoints.c_str());

                sqlite3_finalize(statement);
            }
        }

        return true;
    }
}

void mofocup::addCurrentPlayingTime(std::string bzid, std::string callsign)
{
    if (playingTime.size() > 0)
    {
        for (unsigned int i = 0; i < playingTime.size(); i++)
        {
            if (playingTime.at(i).bzid == bzid)
            {
                int timePlayed = bz_getCurrentTime() - playingTime.at(i).joinTime;

                bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has played for %i seconds. Updating the database...", callsign.c_str(), bzid.c_str(), timePlayed);
                
                std::string updatePlayingTimeQuery = ""
                "INSERT OR REPLACE INTO `PlayingTime` (BZID, Callsign, CupID, PlayingTime) "
                "VALUES (?, "
                "?, "
                "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`), "
                "(SELECT COALESCE((SELECT `PlayingTime` + ? FROM `CTFCup`, `Cups` WHERE `CTFCup`.`BZID` = ? AND `CTFCup`.`CupID` = `Cups`.`CupID` AND `ServerID` = ? AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`), '1')";
                
                sqlite3_stmt *newPlayingTime;

                if (sqlite3_prepare_v2(db, updatePlayingTimeQuery.c_str(), -1, &newPlayingTime, 0) == SQLITE_OK)
                {
                    sqlite3_bind_text(newPlayingTime, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 2, callsign.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 3, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 4, convertToString(timePlayed).c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 5, bzid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 6, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);

                    int cols = sqlite3_column_count(newPlayingTime), result = 0;
                    result = sqlite3_step(newPlayingTime);
                    sqlite3_finalize(newPlayingTime);
                }

                playingTime.erase(playingTime.begin() + i, playingTime.begin() + i + 1);
            }
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

void mofocup::trackNewPlayingTime(std::string bzid)
{
    playingTimeStructure newPlayingTime;

    newPlayingTime.bzid = bzid;
    newPlayingTime.joinTime = bz_getCurrentTime();

    playingTime.push_back(newPlayingTime);
}

void mofocup::updatePlayerRatio(std::string bzid)
{
    float newRankDecimal;
    int result, points, playingTime, oldRank, newRank;
    sqlite3_stmt *currentStats;

    if (sqlite3_prepare_v2(db, "SELECT `Points`, `PlayingTime`, `Rating` FROM `CTFCup` WHERE `BZID` = ?", -1, &currentStats, 0) == SQLITE_OK)
    {
        sqlite3_bind_text(currentStats, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
        int cols = sqlite3_column_count(currentStats), result = sqlite3_step(currentStats);
        points = atoi((char*)sqlite3_column_text(currentStats, 0));
        playingTime = atoi((char*)sqlite3_column_text(currentStats, 1));
        oldRank = atoi((char*)sqlite3_column_text(currentStats, 2));

        sqlite3_finalize(currentStats);
    }

    newRankDecimal = (float)points/(float)((float)playingTime/86400.0);
    newRank = int(newRankDecimal);

    std::string query = ""
    "INSERT OR REPLACE INTO `CTFCup` (BZID, CupID, Callsign, Points, Rating, PlayingTime) "
    "VALUES (?, "
    "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
    "(SELECT `Callsign` FROM `CTFCup`, `Cups` WHERE `CTFCup`.`BZID` = ? AND `CTFCup`.`CupID` = `Cups`.`CupID` and `ServerID` = ? AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
    "(SELECT `Points` FROM `CTFCup`, `Cups` WHERE `CTFCup`.`BZID` = ? AND `CTFCup`.`CupID` = `Cups`.`CupID` AND `ServerID` = ? AND `CupType` = 'capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
    "?, "
    "(SELECT `PlayingTime` FROM `CTFCup`, `Cups` WHERE `CTFCup`.`BZID` = ? AND `CTFCup`.`CupID` = `Cups`.`CupID` and `ServerID` = ? AND `CupType` ='capture' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`))";

    sqlite3_stmt *newStats;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &newStats, 0) == SQLITE_OK)
    {
        sqlite3_bind_text(newStats, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 2, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 3, bzid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 4, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 5, bzid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 6, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 7, convertToString(newRank).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 8, bzid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 9, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);

        int cols = sqlite3_column_count(newStats), result = 0;
        result = sqlite3_step(newStats);
        sqlite3_finalize(newStats);
    }
}
