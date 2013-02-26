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
0.9.8
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

    virtual const char* Name (){return "MoFo Cup (A1)";}
    virtual void Init(const char* /*commandLine*/);
    virtual void Cleanup(void);

    virtual void Event(bz_EventData *eventData);
    virtual bool SlashCommand(int playerID, bz_ApiString command, bz_ApiString message, bz_APIStringList *params);

    virtual void addCurrentPlayingTime(std::string bzid, std::string callsign);
    virtual std::string convertToString(int myInt);
    virtual std::string convertToString(double myDouble);
    virtual void doQuery(std::string query);
    virtual void incrementPoints(std::string bzid, std::string cup, std::string pointsToIncrement);
    virtual int playersKilledByGenocide(bz_eTeamType killerTeam);
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
std::string cups[] = {"Bounty", "CTF", "Geno", "Kills"};
//Keep track of bounties
int numberOfKills[256] = {0}; //the bounty a player has on their turret
int rampage[8] = {0, 6, 12, 18, 24, 30, 36, 999}; //rampages
int lastPlayerDied = -1; //the last person who was killed
int flagID = -1; //if the flag id is either 0 or 1, it's a team flag
double timeDropped = 0; //the time a team flag was dropped

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

        if (db != 0) //if the database connection succeed and the database is empty, let's create the tables needed
        {
            doQuery("CREATE TABLE IF NOT EXISTS \"Players\" (\"BZID\" INTEGER NOT NULL UNIQUE DEFAULT (0), \"Callsign\" TEXT NOT NULL DEFAULT ('Anonymous'), \"CupID\" INTEGER NOT NULL DEFAULT (0), \"PlayingTime\" INTEGER NOT NULL DEFAULT (0));");
            doQuery("CREATE TABLE IF NOT EXISTS \"CTFCup\" (\"BZID\" INTEGER NOT NULL UNIQUE DEFAULT (0),\"CupID\" INTEGER NOT NULL DEFAULT (0), \"Points\" INTEGER NOT NULL DEFAULT (0), \"Rating\" INTEGER NOT NULL DEFAULT (0));");
            doQuery("CREATE TABLE IF NOT EXISTS \"BountyCup\" (\"BZID\" INTEGER NOT NULL UNIQUE DEFAULT (0), \"CupID\" INTEGER NOT NULL DEFAULT (0), \"Points\" INTEGER NOT NULL DEFAULT (0), \"Rating\" INTEGER NOT NULL DEFAULT (0));");
            doQuery("CREATE TABLE IF NOT EXISTS \"GenoCup\" (\"BZID\" INTEGER NOT NULL UNIQUE DEFAULT (0), \"CupID\" INTEGER NOT NULL DEFAULT (0), \"Points\" INTEGER NOT NULL DEFAULT (0), \"Rating\" INTEGER NOT NULL DEFAULT (0));");
            doQuery("CREATE TABLE IF NOT EXISTS \"KillsCup\" (\"BZID\" INTEGER NOT NULL UNIQUE DEFAULT (0), \"CupID\" INTEGER NOT NULL DEFAULT (0), \"Points\" INTEGER NOT NULL DEFAULT (0), \"Rating\" INTEGER NOT NULL DEFAULT (0));");
            doQuery("CREATE TABLE IF NOT EXISTS \"Cups\" (\"CupID\" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, \"ServerID\" TEXT NOT NULL, \"StartTime\" REAL NOT NULL, \"EndTime\" REAL NOT NULL);");
        }

        //unload the plugin if any events fail to register
        if (!Register(bz_ePlayerDieEvent) ||
            !Register(bz_eCaptureEvent) ||
            !Register(bz_ePlayerPartEvent) ||
            !Register(bz_ePlayerJoinEvent) ||
            !Register(bz_ePlayerPausedEvent) ||
            !Register(bz_eTickEvent) ||
            !Register(bz_eFlagDroppedEvent))
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
            std::string bzid = bz_getPlayerByIndex(ctfdata->playerCapping)->bzID.c_str(),
                        callsign = bz_getPlayerByIndex(ctfdata->playerCapping)->callsign.c_str();

            if (bzid.empty()) //ignore the cap if it's an unregistered player
                return;

            //update playing time of the capper to accurately calculate the total points
            addCurrentPlayingTime(bzid, callsign);
            trackNewPlayingTime(bzid);

            int bonusPoints = 8 * (bz_getTeamCount(ctfdata->teamCapped) - bz_getTeamCount(ctfdata->teamCapping)) + 3 * bz_getTeamCount(ctfdata->teamCapped); //calculate the amount of bonus points

            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has captured the flag earning %i points towards the CTF Cup", callsign.c_str(), bzid.c_str(), bonusPoints);

            incrementPoints(bzid, "CTF", convertToString(bonusPoints));
            updatePlayerRatio(bzid);
        }
        break;

        case bz_eFlagDroppedEvent:
        {
            bz_FlagDroppedEventData_V1* flagdropdata = (bz_FlagDroppedEventData_V1*)eventData;

            if (flagdropdata->flagID == 0 || flagdropdata->flagID == 1) //check if a team flag was dropped
            {
                lastPlayerDied = flagdropdata->playerID; //store the player who dropped it
                flagID = flagdropdata->flagID; //store the which team flag was dropped
                timeDropped = bz_getCurrentTime(); //store the time
            }
        }
        break;

        case bz_ePlayerDieEvent:
        {
            bz_PlayerDieEventData_V1* diedata = (bz_PlayerDieEventData_V1*)eventData;
            std::string bzid = bz_getPlayerByIndex(diedata->killerID)->bzID.c_str(),
                        callsign = bz_getPlayerByIndex(diedata->killerID)->callsign.c_str();

            if (bzid.empty()) //No need to continue if the player isn't registered
                return;

            /*
                MoFo Cup :: Bounty Cup
                ----------------------

                We will be keeping track of the bounty points a player has
                earned after killing a player with a bounty

            */

            if (diedata->playerID != diedata->killerID) //if it's not a selfkill, increment their bounty
                numberOfKills[diedata->killerID]++;

            //store scoring info
            int killerRampageScore = 0;
            int killerBonusScore = 0;

            if (diedata->playerID == lastPlayerDied && diedata->playerID != diedata->killerID && timeDropped + 3 > bz_getCurrentTime()) //check if a team flag carrier was killed
                killerBonusScore = 2; //2 points for killing a team flag carrier

            if (numberOfKills[diedata->playerID] > 0) //if the player who died had a bounty
            {
                for (int i = 0; i < sizeof(rampage)/sizeof(int); i++) //go through all the rampage levels
                {
                    //figure out what number to multiply by for a bounty total
                    if (numberOfKills[diedata->playerID] >= rampage[i] && numberOfKills[diedata->playerID] < rampage[i+1] && diedata->playerID != diedata->killerID)
                        killerRampageScore = 2 * i;
                }
            }

            if (killerRampageScore + killerBonusScore > 0)
            {
                bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) earned %i total bounty points.", callsign.c_str(), bzid.c_str(), killerRampageScore + killerBonusScore);
                incrementPoints(bzid, "Bounty", convertToString(killerRampageScore + killerBonusScore));
            }

            numberOfKills[diedata->playerID] = 0; //reset the bounty on the player who died to 0

            /*
                MoFo Cup :: Geno Cup
                ----------------------

                Keep track of all the points a player has earned by killing
                a team using genocide

            */

            if (((diedata->flagKilledWith == "R*" && diedata->team != eRedTeam) ||
                diedata->flagKilledWith == "G*" && diedata->team != eGreenTeam ||
                diedata->flagKilledWith == "B*" && diedata->team != eBlueTeam ||
                diedata->flagKilledWith == "P*" && diedata->team != ePurpleTeam) && //check if it's a geno hit
                diedata->team != diedata->killerTeam && //check that it's not affecting the same team
                diedata->playerID != diedata->killerID) //check that it's not a selfkill
            {
                incrementPoints(bzid, "Kills", convertToString(playersKilledByGenocide(diedata->killerTeam) - 1)); //we're increment the kills by 1 below
                incrementPoints(bzid, "Geno", convertToString(playersKilledByGenocide(diedata->killerTeam) + 1));
                bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has got a geno hit earning %i points towards the Geno Cup", callsign.c_str(), bzid.c_str(), playersKilledByGenocide(diedata->killerTeam));
            }

            /*
                MoFo Cup :: Kills Cup
                ---------------------

                Keep track of all the kills a player makes

            */

            incrementPoints(bzid, "Kills", "1");

            //Update the ratios for all the players in all the cups
            updatePlayerRatio(bzid);
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
            std::string bzid = joindata->record->bzID.c_str(),
                        callsign = joindata->record->callsign.c_str();

            if (bzid.empty() || joindata->record->team == eObservers) //don't do anything if the player is an observer or is not registered
                return;

            for (int i = 0; i < sizeof(cups)/sizeof(std::string); i++) //Add players to the database for the first time playing
            {
                doQuery("INSERT OR IGNORE INTO `" + cups[i] + "Cup` VALUES (" + bzid + ", (SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + bz_getPublicAddr().c_str() + "' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 0, 0)");
            }

            doQuery("INSERT OR IGNORE INTO `Players` VALUES (" + bzid + ", '" + callsign + "', (SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + bz_getPublicAddr().c_str() + "' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 0)");
            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has started to play, now recording playing time.", callsign.c_str(), bzid.c_str());
            trackNewPlayingTime(bzid);
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
            std::string bzid = partdata->record->bzID.c_str(),
                        callsign = partdata->record->callsign.c_str();
            numberOfKills[partdata->playerID] = 0;

            if (bzid.empty() || partdata->record->team == eObservers) //don't do anything if the player is an observer or is not registered
                return;

            addCurrentPlayingTime(bzid, callsign); //they left, let's add their playing time to the database
            updatePlayerRatio(bzid);

            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has left. Updated their playing time and ratio.", callsign.c_str(), bzid.c_str());
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
            std::string bzid = bz_getPlayerByIndex(pausedata->playerID)->bzID.c_str(),
                        callsign = bz_getPlayerByIndex(pausedata->playerID)->callsign.c_str();

            if (bzid.empty()) //don't bother if the player isn't registered
                return;

            if (pausedata->pause) //when a player pauses, we add their current playing time to the database
                addCurrentPlayingTime(bzid, callsign);
            else //start tracking a player's playing time when they have unpaused
                trackNewPlayingTime(bzid);
        }
        break;

        case bz_eTickEvent:
        {
            if (bz_getTeamCount(eRedTeam) + bz_getTeamCount(eGreenTeam) + bz_getTeamCount(eBlueTeam) + bz_getTeamCount(ePurpleTeam) == 0)
                return;

            if (lastDatabaseUpdate + 300 < bz_getCurrentTime()) //Update player ratios every 5 minutes
            {
                lastDatabaseUpdate = bz_getCurrentTime(); //Get the current time

                bz_APIIntList *playerList = bz_newIntList();
                bz_getPlayerIndexList(playerList);

                for (unsigned int i = 0; i < playerList->size(); i++) //Go through all the players
                {
                    std::string bzid = bz_getPlayerByIndex(playerList->get(i))->bzID.c_str(),
                                callsign = bz_getPlayerByIndex(playerList->get(i))->callsign.c_str();

                    if (bzid.empty()) //Go to the next player if this player isn't registered
                        continue;

                    addCurrentPlayingTime(bzid, callsign);
                    updatePlayerRatio(bzid);
                    trackNewPlayingTime(bzid);
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
        if (strcmp(params->get(0).c_str(), "bounty") == 0 ||
            strcmp(params->get(0).c_str(), "ctf") == 0 ||
            strcmp(params->get(0).c_str(), "geno") == 0 ||
            strcmp(params->get(0).c_str(), "kills") == 0)
        {
            std::string cup;

            if (strcmp(params->get(0).c_str(), "bounty") == 0) cup = "Bounty";
            else if (strcmp(params->get(0).c_str(), "ctf") == 0) cup = "CTF";
            else if (strcmp(params->get(0).c_str(), "geno") == 0) cup = "Geno";
            else if (strcmp(params->get(0).c_str(), "kills") == 0) cup = "Kills";

            std::string top5query = "SELECT `Players`.`Callsign`, `" + cup + "Cup`.`Rating` FROM `" + cup + "Cup`, `Players` WHERE `Players`.`BZID` = `" + cup + "Cup`.`BZID` AND `" + cup + "Cup`.`CupID` = (SELECT `Cups`.`CupID` FROM `Cups` WHERE `Cups`.`ServerID` = ? AND strftime('%s','now') < `Cups`.`EndTime` AND strftime('%s','now') > `Cups`.`StartTime`) ORDER BY `" + cup + "Cup`.`Rating` DESC, `Players`.`PlayingTime` ASC LIMIT 5";
            std::string myPlaceQuery = "SELECT `Rating`, (SELECT COUNT(*) FROM `" + cup + "Cup` AS c2 WHERE c2.Rating > c1.Rating) + 1 AS row_Num FROM `" + cup + "Cup` AS c1 WHERE `BZID` = ?";
            sqlite3_stmt *statement;

            if (sqlite3_prepare_v2(db, top5query.c_str(), -1, &statement, 0) == SQLITE_OK)
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

            if (sqlite3_prepare_v2(db, myPlaceQuery.c_str(), -1, &statement, 0) == SQLITE_OK)
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
            for (int i = 0; i < sizeof(cups)/sizeof(std::string); i++) //go through each cup
            {
                std::string query = "SELECT `Rating`, (SELECT COUNT(*) FROM `" + cups[i] + "Cup` AS c2 WHERE c2.Rating > c1.Rating) + 1 AS row_Num FROM `CTFCup` AS c1 WHERE `Callsign` = ?";

                if (sqlite3_prepare_v2(db, query.c_str(), -1, &statement, 0) == SQLITE_OK)
                {
                    sqlite3_bind_text(statement, 1, params->get(0).c_str(), -1, SQLITE_TRANSIENT);
                    int result = sqlite3_step(statement);

                    if (result == SQLITE_ROW)
                    {
                        std::string playerPoints = (char*)sqlite3_column_text(statement, 0);
                        std::string playerRatio = (char*)sqlite3_column_text(statement, 1);
                        bz_sendTextMessagef(BZ_SERVER, playerID, "%s is currently #%s in the %s Cup with a score of %s", params->get(0).c_str(), cups[i].c_str(), playerRatio.c_str(), playerPoints.c_str());
                    }
                    else
                    {
                        bz_sendTextMessagef(BZ_SERVER, playerID, "%s is not part of the current MoFo Cup.", params->get(0).c_str());
                        break;
                    }

                    sqlite3_finalize(statement);
                }
            }
        }
        else
        {
            for (int i = 0; i < sizeof(cups)/sizeof(std::string); i++) //go through each cup
            {
                std::string query = "SELECT `Rating`, (SELECT COUNT(*) FROM `" + cups[i] + "Cup` AS c2 WHERE c2.Rating > c1.Rating) + 1 AS row_Num FROM `" + cups[i] + "Cup` AS c1 WHERE `BZID` = ?";
                if (sqlite3_prepare_v2(db, query.c_str(), -1, &statement, 0) == SQLITE_OK)
                {
                    sqlite3_bind_text(statement, 1, std::string(bz_getPlayerByIndex(playerID)->bzID.c_str()).c_str(), -1, SQLITE_TRANSIENT);
                    int result = sqlite3_step(statement);

                    if (result == SQLITE_ROW)
                    {
                        std::string playerPoints = (char*)sqlite3_column_text(statement, 0);
                        std::string playerRatio = (char*)sqlite3_column_text(statement, 1);
                        bz_sendTextMessagef(BZ_SERVER, playerID, "You are currently #%s in %s Cup with a score of %s", playerRatio.c_str(), cups[i].c_str(), playerPoints.c_str());
                    }
                    else
                    {
                        bz_sendTextMessage(BZ_SERVER, playerID, "You are not part of the MoFo Cup yet. Get in there and cap or kill someone!");
                        break;
                    }

                    sqlite3_finalize(statement);
                }
            }
        }

        return true;
    }
}

void mofocup::addCurrentPlayingTime(std::string bzid, std::string callsign)
{
    /*
        This function will add a player's current playing time
        to the `Players` table
    */

    if (playingTime.size() > 0) //As long as we have some playing stored
    {
        for (unsigned int i = 0; i < playingTime.size(); i++) //Go through all the stored playing times
        {
            if (strcmp(playingTime.at(i).bzid.c_str(), bzid.c_str()) == 0) //We found the playing time stored for the specified BZID
            {
                double timePlayed = bz_getCurrentTime() - playingTime.at(i).joinTime; //get the player's playing time

                bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has played for %d seconds. Updating the database...", callsign.c_str(), bzid.c_str(), timePlayed);

                //the query
                std::string updatePlayingTimeQuery = ""
                "INSERT OR REPLACE INTO `Players` (BZID, Callsign, CupID, PlayingTime) "
                "VALUES (?, "
                "?, "
                "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`), "
                "(SELECT COALESCE((SELECT `PlayingTime` + ? FROM `Players`, `Cups` WHERE `Players`.`BZID` = ? AND `ServerID` = ? AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`), ?)))";

                sqlite3_stmt *newPlayingTime;

                if (sqlite3_prepare_v2(db, updatePlayingTimeQuery.c_str(), -1, &newPlayingTime, 0) == SQLITE_OK)
                {
                    //build the query
                    sqlite3_bind_text(newPlayingTime, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 2, callsign.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 3, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 4, convertToString(timePlayed).c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 5, bzid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 6, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(newPlayingTime, 7, convertToString(timePlayed).c_str(), -1, SQLITE_TRANSIENT);

                    //prepare to execute and execute the query
                    int result = sqlite3_step(newPlayingTime);
                    sqlite3_finalize(newPlayingTime);
                }
                else
                {
                    bz_debugMessagef(2, "DEBUG :: MoFo Cup :: SQLite :: addCurrentPlayingTime() :: Error #%i: %s", sqlite3_errcode(db), sqlite3_errmsg(db));
                }

                playingTime.erase(playingTime.begin() + i, playingTime.begin() + i + 1); //remove this stored time from the structure
            }
        }
    }
}

std::string mofocup::convertToString(int myInt)
{
    /*
        Convert an integer into a string
    */

    std::string myString;
    std::stringstream string;
    string << myInt; //Use an stringstream to pass an int through a string
    myString = string.str();

    return myString;
}

std::string mofocup::convertToString(double myDouble)
{
    std::ostringstream myString;

    if (!(myString << myDouble))
        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: Error converting %d to a string", myDouble);

    return myString.str();
}

void mofocup::doQuery(std::string query)
{
    /*
        Execute a SQL query without the need of any return values
    */

    bz_debugMessage(2, "DEBUG :: MoFo Cup :: Executing following SQL query...");
    bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", query.c_str());

    char* db_err = 0; //a place to store the error
    int ret = sqlite3_exec(db, query.c_str(), NULL, 0, &db_err); //execute

    if (db_err != 0) //print out any errors
    {
        bz_debugMessage(2, "DEBUG :: MoFo Cup :: SQL ERROR!");
        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s", db_err);
    }
}

void mofocup::incrementPoints(std::string bzid, std::string cup, std::string pointsToIncrement)
{
    /*
        Increment a player's points in the respective table by the
        specified value
    */

    bz_debugMessagef(4, "DEBUG :: MoFo Cup :: incrementPoints() receiving...");
    bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   BZID -> %s", bzid.c_str());
    bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   Cup -> %s", cup.c_str());
    bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   Points -> %s", pointsToIncrement.c_str());

    //the query
    sqlite3_stmt *newStats;
    std::string query = ""
    "INSERT OR REPLACE INTO `" + cup + "Cup` (BZID, CupID, Points, Rating) "
    "VALUES (?, "
    "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
    "(SELECT COALESCE((SELECT `Points` + ? FROM `" + cup + "Cup`, `Cups` WHERE `" + cup + "Cup`.`BZID` = ? AND `" + cup + "Cup`.`CupID` = `Cups`.`CupID` AND `ServerID` = ? AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), ?)), "
    "(SELECT `Rating` FROM `" + cup + "Cup` WHERE `BZID` = ?))";

    bz_debugMessagef(4, "DEBUG :: MoFo Cup :: SQLite :: Executing query...");
    bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   %s", query.c_str());

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &newStats, 0) == SQLITE_OK)
    {
        //build the query
        sqlite3_bind_text(newStats, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 2, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 3, pointsToIncrement.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 4, bzid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 5, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 6, pointsToIncrement.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(newStats, 7, bzid.c_str(), -1, SQLITE_TRANSIENT);

        //execute
        int result = sqlite3_step(newStats);
        sqlite3_finalize(newStats);
    }
    else //could not prepare the statement
    {
        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: SQLite :: incrementPoints() :: Error #%i: %s", sqlite3_errcode(db), sqlite3_errmsg(db));
    }
}

int mofocup::playersKilledByGenocide(bz_eTeamType killerTeam)
{
    /*
        Get the amount of players that have been ideally killed
        by a genocide hit because not all players on a team will
        be spawned at the time of the hit
    */

    int playerCount = 0; //the value we'll return
    bz_APIIntList *playerList = bz_newIntList();
    bz_getPlayerIndexList(playerList);

    for (unsigned int i = 0; i < playerList->size(); i++) //go through all the players
    {
        //check a player is part of the team affected, not an observer, and is spawned
        if (bz_getPlayerByIndex(playerList->get(i))->team != killerTeam &&
            bz_getPlayerByIndex(playerList->get(i))->team != eObservers &&
            bz_getPlayerByIndex(playerList->get(i))->spawned)
            playerCount++;
    }

    bz_deleteIntList(playerList);

    return playerCount;
}

void mofocup::trackNewPlayingTime(std::string bzid)
{
    /*
        Create a new slot in the structure in order to keep track
        of a player's playing time
    */

    playingTimeStructure newPlayingTime;

    newPlayingTime.bzid = bzid;
    newPlayingTime.joinTime = bz_getCurrentTime();

    playingTime.push_back(newPlayingTime);
}

void mofocup::updatePlayerRatio(std::string bzid)
{
    /*
        Go through all the cups, and update each player's ratio in the table
        for the appropriate cup
    */

    for (int i = 0; i < sizeof(cups)/sizeof(std::string); i++) //go through each cup
    {
        bz_debugMessagef(4, "DEBUG :: MoFo Cup :: Updating (%s) player stats for player BZID -> %s", cups[i].c_str(), bzid.c_str());

        //initialize variables, and build a query for the respective table/cup to get the values to calculate a new ratio
        float newRankDecimal;
        int result, points, playingTime, oldRank, newRank;
        std::string currentStatsQuery = "SELECT `" + cups[i] + "Cup`.`Points`, `Players`.`PlayingTime`, `" + cups[i] + "Cup`.`Rating` FROM `" + cups[i] + "Cup`, `Players` WHERE `Players`.`BZID` = `" + cups[i] + "Cup`.`BZID` AND `" + cups[i] + "Cup`.`BZID` = ?";
        sqlite3_stmt *currentStats;

        if (sqlite3_prepare_v2(db, currentStatsQuery.c_str(), -1, &currentStats, 0) == SQLITE_OK)
        {
            sqlite3_bind_text(currentStats, 1, bzid.c_str(), -1, SQLITE_TRANSIENT); //build the query
            int result = sqlite3_step(currentStats);

            //store the values needed
            points = atoi((char*)sqlite3_column_text(currentStats, 0));
            playingTime = atoi((char*)sqlite3_column_text(currentStats, 1));
            oldRank = atoi((char*)sqlite3_column_text(currentStats, 2));

            bz_debugMessagef(4, "DEBUG :: MoFo Cup :: %s Stats for BZID %s", cups[i].c_str(), bzid.c_str());
            bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   Points        -> %i", points);
            bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   Playing Time  -> %i", playingTime);
            bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   Old Ratio     -> %i", oldRank);

            sqlite3_finalize(currentStats);
        }
        else
        {
            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: SQLite :: updatePlayerRatio() [1] :: Error #%i: %s", sqlite3_errcode(db), sqlite3_errmsg(db));
        }

        //calculate the new ratio
        newRankDecimal = (float)points/(float)((float)playingTime/86400.0);
        newRank = int(newRankDecimal);

        bz_debugMessagef(4, "DEBUG :: MoFo Cup :: New ratio for BZID %s -> %i ~= %f", bzid.c_str(), newRank, newRankDecimal);

        //build the query to update the ratios in each respective table
        std::string query = ""
        "INSERT OR REPLACE INTO `" + cups[i] + "Cup` (BZID, CupID, Points, Rating) "
        "VALUES (?, "
        "(SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
        "(SELECT `Points` FROM `" + cups[i] + "Cup`, `Cups` WHERE `" + cups[i] + "Cup`.`BZID` = ? AND `" + cups[i] + "Cup`.`CupID` = `Cups`.`CupID` AND `ServerID` = ? AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), "
        "?)";

        sqlite3_stmt *newStats;

        if (sqlite3_prepare_v2(db, query.c_str(), -1, &newStats, 0) == SQLITE_OK)
        {
            //prepare the query
            sqlite3_bind_text(newStats, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(newStats, 2, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(newStats, 3, bzid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(newStats, 4, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(newStats, 5, convertToString(newRank).c_str(), -1, SQLITE_TRANSIENT);

            //execute
            int result = sqlite3_step(newStats);

            if (result == SQLITE_DONE)
                bz_debugMessagef(4, "DEBUG :: MoFo Cup :: %s ratio updated successfully for BZID %s", cups[i].c_str(), bzid.c_str());
            else
                bz_debugMessagef(4, "DEBUG :: MoFo Cup :: %s ratio updated failed for BZID %s", cups[i].c_str(), bzid.c_str());

            sqlite3_finalize(newStats);
        }
        else
        {
            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: SQLite :: updatePlayerRatio() [2] :: Error #%i: %s", sqlite3_errcode(db), sqlite3_errmsg(db));
        }
    }
}
