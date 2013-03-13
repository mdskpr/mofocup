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
1.2.1
*/

#include <iostream>
#include <fstream>
#include <map>
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

    virtual const char* Name (){return "MoFo Cup [RC 5]";}
    virtual void Init(const char* commandLine);
    virtual void Cleanup(void);

    virtual void Event(bz_EventData *eventData);
    virtual bool SlashCommand(int playerID, bz_ApiString command, bz_ApiString message, bz_APIStringList *params);

    virtual void addCurrentPlayingTime(std::string bzid, std::string callsign);
    virtual void cleanCup(void);
    virtual std::string convertToString(int myInt);
    virtual std::string convertToString(double myDouble);
    virtual void doQuery(std::string query);
    virtual std::string formatScore(std::string place, std::string callsign, std::string points);
    virtual std::vector<std::string> getPlayerInCupStanding(std::string cup, std::string place);
    virtual std::vector<std::string> getPlayerStandingFromBZID(std::string cup, std::string bzid);
    virtual std::vector<std::string> getPlayerStandingFromCallsign(std::string cup, std::string callsign);
    virtual bool isDigit(std::string someString);
    virtual bool isFirstTime(std::string bzid);
    virtual bool isPlayerAvailable(std::string bzid);
    virtual bool isValidPlayerID(int playerID);
    virtual void incrementPoints(std::string bzid, std::string cup, std::string pointsToIncrement);
    virtual int playersKilledByGenocide(bz_eTeamType killerTeam);
    virtual sqlite3_stmt* prepareQuery(std::string sql);
    virtual void startCup(void);
    virtual void trackNewPlayingTime(std::string bzid);
    virtual void updatePlayerRatio(std::string bzid);

    //we're storing the time people play so we can rank players based on how quick they make as many caps
    struct playingTimeStructure
    {
        std::string bzid;
        double joinTime;
    };
    std::vector<playingTimeStructure> playingTime;

    std::string top5Players[4][5][3]; //0 - Bounty | 1 - CTF | 2 - Geno | 3 - Kills
    double lastDatabaseUpdate;
    typedef std::map<std::string, sqlite3_stmt*> PreparedStatementMap; // Define the type as a shortcut
    PreparedStatementMap preparedStatements; // Create the object to store prepared statements

    sqlite3_stmt *addCurrentPlayingTimeStmt, *getPlayerInCupStandingStmt, *getPlayerStandingFromBZIDStmt, *getPlayerStandingFromCallsignStmt, *isFirstTimeStmt,
        *incrementPointsStmt, *getCurrentPlayerStatsStmt, *updatePlayerRatioStmt;
};

BZ_PLUGIN(mofocup);

//Initialize all the available cups
std::string cups[] = {"Bounty", "CTF", "Geno", "Kill"};
//Keep track of bounties
int numberOfKills[256] = {0}; //the bounty a player has on their turret
int rampage[8] = {0, 6, 12, 18, 24, 30, 36, 999}; //rampages
int lastPlayerDied = -1; //the last person who was killed
int flagID = -1; //if the flag id is either 0 or 1, it's a team flag
double timeDropped = 0; //the time a team flag was dropped
int bountyPoints[253] = {0}, genoPoints[253] = {0}, killPoints[253] = {0};

void mofocup::Init(const char* commandLine)
{
    bz_registerCustomSlashCommand("cup", this); //register the /cup command
    bz_registerCustomSlashCommand("rank", this); //register the /rank command
    bz_registerCustomSlashCommand("refreshcup", this); //register the /refreshcup command

    if (commandLine == NULL || std::string(commandLine).empty()) //no database provided, unloadplugin ourselves
    {
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: Please provide a filename for the database");
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: -loadplugin /path/to/mofocup.so,/path/to/database.db");
        bz_debugMessage(0, "DEBUG :: MoFo Cup :: Unloading MoFoCup plugin...");
        bz_unloadPlugin(Name());
    }

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
        doQuery("CREATE TABLE IF NOT EXISTS \"Cups\" (\"CupID\" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, \"ServerID\" TEXT NOT NULL, \"StartTime\" REAL NOT NULL, \"EndTime\" REAL NOT NULL);");
        doQuery("CREATE TABLE IF NOT EXISTS \"Points\" (\"CupType\" TEXT NOT NULL, \"BZID\" INTEGER NOT NULL, \"CupID\" INTEGER NOT NULL, \"Points\" INTEGER NOT NULL, \"Ratio\" INTEGER NOT NULL)");
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

    startCup();
    bz_debugMessage(4, "DEBUG :: MoFo Cup :: Successfully loaded and database connection ready.");
}

void mofocup::Cleanup()
{
    Flush();
    bz_removeCustomSlashCommand("cup");
    bz_removeCustomSlashCommand("rank");
    bz_removeCustomSlashCommand("refreshcup");

    cleanCup();
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

            if (!isValidPlayerID(flagdropdata->playerID))
                return;

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

            if (diedata->killerID == 253) //ignore kills made by world weapons
                return;

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
                bountyPoints[diedata->killerID] += killerRampageScore + killerBonusScore;
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
                genoPoints[diedata->killerID] += playersKilledByGenocide(diedata->killerTeam) + 1;
                bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has got a geno hit earning %i points towards the Geno Cup", callsign.c_str(), bzid.c_str(), playersKilledByGenocide(diedata->killerTeam));
            }

            /*
                MoFo Cup :: Kills Cup
                ---------------------

                Keep track of all the kills a player makes

            */

            killPoints[diedata->killerID] += 1;
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

            if (isFirstTime(bzid)) //introduce players into the MoFo Cup
            {
                bz_sendTextMessagef(BZ_SERVER, joindata->playerID, "Welcome %s! By playing on Apocalypse, you have been entered to this month's MoFo Cup.", callsign.c_str());
                bz_sendTextMessagef(BZ_SERVER, joindata->playerID, "The MoFo Cup is a monthly tournament that consists of the most Bounty, CTF, Geno hits, and kills a player has made.");
                bz_sendTextMessagef(BZ_SERVER, joindata->playerID, "Type '/help cup' for more information about the MoFo Cup!");

                for (int i = 0; i < sizeof(cups)/sizeof(std::string); i++) //Add players to the database for the first time playing
                {
                    doQuery("INSERT OR IGNORE INTO `Points` VALUES ('" + cups[i] + "', " + bzid + ", (SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + bz_getPublicAddr().c_str() + "' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 0, 0)");
                }

                doQuery("INSERT OR IGNORE INTO `Players` VALUES (" + bzid + ", '" + callsign + "', (SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + bz_getPublicAddr().c_str() + "' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 1)");
            }

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

            if (bountyPoints[partdata->playerID] > 0) incrementPoints(bzid, "Bounty", convertToString(bountyPoints[partdata->playerID]));
            if (genoPoints[partdata->playerID] > 0) incrementPoints(bzid, "Geno", convertToString(genoPoints[partdata->playerID]));
            if (killPoints[partdata->playerID] > 0) incrementPoints(bzid, "Kill", convertToString(killPoints[partdata->playerID]));

            updatePlayerRatio(bzid);

            bountyPoints[partdata->playerID] = 0;
            genoPoints[partdata->playerID] = 0;
            killPoints[partdata->playerID] = 0;

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

                    if (bountyPoints[playerList->get(i)] > 0) incrementPoints(bzid, "Bounty", convertToString(bountyPoints[playerList->get(i)]));
                    if (genoPoints[playerList->get(i)] > 0) incrementPoints(bzid, "Geno", convertToString(genoPoints[playerList->get(i)]));
                    if (killPoints[playerList->get(i)] > 0) incrementPoints(bzid, "Kill", convertToString(killPoints[playerList->get(i)]));

                    bountyPoints[playerList->get(i)] = 0;
                    genoPoints[playerList->get(i)] = 0;
                    killPoints[playerList->get(i)] = 0;

                    updatePlayerRatio(bzid);
                    trackNewPlayingTime(bzid);
                }

                bz_deleteIntList(playerList);

                for (int i = 0; i < 4; i++) //loop through all the cups
                {
                    for (int j = 0; j < 5; j++) //loop through the top 5 players
                    {
                        std::vector<std::string> getPlayerInformation = getPlayerInCupStanding(cups[i], convertToString(j));

                        if (strcmp(getPlayerInformation[2].c_str(), top5Players[i][j][2].c_str()) != 0) //if a player has a new position in the top 5
                        {
                            if (isPlayerAvailable(getPlayerInformation[2])) //if the player is playing on the server, announce it
                                bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "Congrats to %s for being #%i in the %s Cup!!!", getPlayerInformation[0].c_str(), j + 1, cups[i].c_str());

                            //update the player stats
                            top5Players[i][j][0] = getPlayerInformation[0];
                            top5Players[i][j][1] = getPlayerInformation[1];
                            top5Players[i][j][2] = getPlayerInformation[2];
                        }
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
        if (strcmp(params->get(0).c_str(), "bounty") == 0 ||
            strcmp(params->get(0).c_str(), "ctf") == 0 ||
            strcmp(params->get(0).c_str(), "geno") == 0 ||
            strcmp(params->get(0).c_str(), "kills") == 0)
        {
            std::string cup;

            if (strcmp(params->get(0).c_str(), "bounty") == 0) cup = "Bounty";
            else if (strcmp(params->get(0).c_str(), "ctf") == 0) cup = "CTF";
            else if (strcmp(params->get(0).c_str(), "geno") == 0) cup = "Geno";
            else if (strcmp(params->get(0).c_str(), "kills") == 0) cup = "Kill";

            bz_sendTextMessagef(BZ_SERVER, playerID, "Planet MoFo %s Cup", cup.c_str());
            bz_sendTextMessage(BZ_SERVER, playerID, "--------------------");
            bz_sendTextMessage(BZ_SERVER, playerID, "        Callsign                    Points");

            for (int i = 0; i < 5; i++) //get the stats for top 5 players
            {
                std::vector<std::string> playerInfo = getPlayerInCupStanding(cup, convertToString(i));

                bz_sendTextMessage(BZ_SERVER, playerID, formatScore(convertToString(i + 1), playerInfo[0], playerInfo[1]).c_str());
            }

            if (std::string(bz_getPlayerByIndex(playerID)->bzID.c_str()).empty()) //check if player is registered to display their stats
                return true;

            bz_sendTextMessage(BZ_SERVER, playerID, " "); //nice little space

            std::vector<std::string> myPlayerInfo = getPlayerStandingFromBZID(cup, bz_getPlayerByIndex(playerID)->bzID.c_str()); //get player's stats

            bz_sendTextMessage(BZ_SERVER, playerID, formatScore(myPlayerInfo[0], bz_getPlayerByIndex(playerID)->callsign.c_str(), myPlayerInfo[1]).c_str());
        }
        else //give the user some help
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "Usage: /cup <bounty | ctf | geno | kills>");
            bz_sendTextMessage(BZ_SERVER, playerID, "See '/help cup' for more information regarding the MoFo Cup.");
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

        std::string callsignToLookup; //store the callsign we're going to search for

        for (unsigned int i = 0; i < params->size(); i++) //piece together the callsign from the slash command parameters
        {
            callsignToLookup += params->get(i).c_str();
            if (i != params->size() - 1) // so we don't stick a whitespace on the end
                callsignToLookup += " "; // add a whitespace between each chat text parameter
        }

        if (strcmp(params->get(0).c_str(), "") != 0) //if we are searching for a callsign
        {
            for (int i = 0; i < sizeof(cups)/sizeof(std::string); i++) //go through each cup
            {
                std::vector<std::string> playerRank = getPlayerStandingFromCallsign(cups[i], callsignToLookup);

                if (strcmp(playerRank[0].c_str(), "-1") == 0)
                    bz_sendTextMessagef(BZ_SERVER, playerID, "%s is not part of the current MoFo Cup.", callsignToLookup.c_str());
                else
                    bz_sendTextMessagef(BZ_SERVER, playerID, "%s is currently #%s in the %s Cup with a score of %s", callsignToLookup.c_str(), playerRank[0].c_str(), cups[i].c_str(), playerRank[1].c_str());
            }
        }
        else
        {
            for (int i = 0; i < sizeof(cups)/sizeof(std::string); i++) //go through each cup
            {
                std::vector<std::string> playerRank = getPlayerStandingFromBZID(cups[i], bz_getPlayerByIndex(playerID)->bzID.c_str());

                if (strcmp(playerRank[0].c_str(), "-1") == 0)
                    bz_sendTextMessage(BZ_SERVER, playerID, "You are not part of the MoFo Cup yet. Get in there and cap or kill someone!");
                else
                    bz_sendTextMessagef(BZ_SERVER, playerID, "You are currently #%s in the %s Cup with a score of %s", playerRank[0].c_str(), cups[i].c_str(), playerRank[1].c_str());
            }
        }

        return true;
    }
    else if (command == "refreshcup")
    {
        if (bz_hasPerm(playerID, "mofocup"))
        {
            bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, "WARNING: There may be lag or jitter spikes for the next minute or so.");
            bz_sendTextMessagef(BZ_SERVER, eAdministrators, "%s has requested the MoFo Cup database to be forcefully updated.", bz_getPlayerByIndex(playerID)->callsign.c_str());

            cleanCup();
            startCup();
        }
        else
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "You must authenticate yourself in order to run this command.");
        }
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
                int timePlayed = bz_getCurrentTime() - playingTime.at(i).joinTime; //get the player's playing time

                bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has played for %i seconds. Updating the database...", callsign.c_str(), bzid.c_str(), timePlayed);

                //build the query
                sqlite3_bind_text(addCurrentPlayingTimeStmt, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addCurrentPlayingTimeStmt, 2, convertToString(timePlayed).c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addCurrentPlayingTimeStmt, 3, bzid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addCurrentPlayingTimeStmt, 4, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);

                //prepare to execute and execute the query
                sqlite3_step(addCurrentPlayingTimeStmt);
                sqlite3_reset(addCurrentPlayingTimeStmt);

                playingTime.erase(playingTime.begin() + i, playingTime.begin() + i + 1); //remove this stored time from the structure
            }
        }
    }
}

void mofocup::cleanCup(void)
{
    bz_APIIntList *playerList = bz_newIntList();
    bz_getPlayerIndexList(playerList);

    for (unsigned int i = 0; i < playerList->size(); i++) //Go through all the players
    {
        std::string bzid = bz_getPlayerByIndex(playerList->get(i))->bzID.c_str(),
                    callsign = bz_getPlayerByIndex(playerList->get(i))->callsign.c_str();
        numberOfKills[playerList->get(i)] = 0;

        if (bzid.empty() || bz_getPlayerByIndex(playerList->get(i))->team == eObservers) //don't do anything if the player is an observer or is not registered
            return;

        addCurrentPlayingTime(bzid, callsign); //they left, let's add their playing time to the database

        if (bountyPoints[playerList->get(i)] > 0) incrementPoints(bzid, "Bounty", convertToString(bountyPoints[playerList->get(i)]));
        if (genoPoints[playerList->get(i)] > 0) incrementPoints(bzid, "Geno", convertToString(genoPoints[playerList->get(i)]));
        if (killPoints[playerList->get(i)] > 0) incrementPoints(bzid, "Kill", convertToString(killPoints[playerList->get(i)]));

        updatePlayerRatio(bzid);

        bountyPoints[playerList->get(i)] = 0;
        genoPoints[playerList->get(i)] = 0;
        killPoints[playerList->get(i)] = 0;

        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: Stats recorded for %s (%s) while preparing for plugin clean up.", callsign.c_str(), bzid.c_str());
    }

    bz_deleteIntList(playerList);

    sqlite3_finalize(addCurrentPlayingTimeStmt);
    sqlite3_finalize(getPlayerInCupStandingStmt);
    sqlite3_finalize(getPlayerStandingFromBZIDStmt);
    sqlite3_finalize(getPlayerStandingFromCallsignStmt);
    sqlite3_finalize(isFirstTimeStmt);
    sqlite3_finalize(incrementPointsStmt);
    sqlite3_finalize(getCurrentPlayerStatsStmt);
    sqlite3_finalize(updatePlayerRatioStmt);

    if (db != NULL) //close the database connection since we won't need it
      sqlite3_close(db);
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
    /*
        Convert an double into a string
    */

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

std::string mofocup::formatScore(std::string place, std::string callsign, std::string points)
{
    /*
        Format the leader board with spacing
    */

    place = "#" + place;
    if (callsign.length() >= 26) callsign = callsign.substr(0, 26);
    while (place.length() < 8) place += " ";
    while (callsign.length() < 28) callsign += " ";
    while (points.length() < 6) points += " ";

    return (place + callsign + points);
}

std::vector<std::string> mofocup::getPlayerInCupStanding(std::string cup, std::string place)
{
    /*
        Get the information for the Nth player in the cup
    */

    std::vector<std::string> playerStats(3);

    sqlite3_bind_text(getPlayerInCupStandingStmt, 1, cup.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(getPlayerInCupStandingStmt, 2, cup.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(getPlayerInCupStandingStmt, 3, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(getPlayerInCupStandingStmt, 4, place.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(getPlayerInCupStandingStmt) == SQLITE_ROW)
    {
        if ((char*)sqlite3_column_text(getPlayerInCupStandingStmt, 0) != NULL ||
            (char*)sqlite3_column_text(getPlayerInCupStandingStmt, 1) != NULL ||
            (char*)sqlite3_column_text(getPlayerInCupStandingStmt, 2) != NULL)
        {
            playerStats[0] = (char*)sqlite3_column_text(getPlayerInCupStandingStmt, 0);
            playerStats[1] = (char*)sqlite3_column_text(getPlayerInCupStandingStmt, 1);
            playerStats[2] = (char*)sqlite3_column_text(getPlayerInCupStandingStmt, 2);

            sqlite3_reset(getPlayerInCupStandingStmt);
            return playerStats;
        }
    }

    playerStats[0] = "Anonymous";
    playerStats[1] = "-1";
    playerStats[2] = "0";

    sqlite3_reset(getPlayerInCupStandingStmt);
    return playerStats;
}

std::vector<std::string> mofocup::getPlayerStandingFromBZID(std::string cup, std::string bzid)
{
    /*
        Get the information for a player based on callsign
    */

    std::vector<std::string> playerStats(2);

    sqlite3_bind_text(getPlayerStandingFromBZIDStmt, 1, cup.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(getPlayerStandingFromBZIDStmt, 2, bzid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(getPlayerStandingFromBZIDStmt, 3, cup.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(getPlayerStandingFromBZIDStmt) == SQLITE_ROW)
    {
        if ((char*)sqlite3_column_text(getPlayerStandingFromBZIDStmt, 0) != NULL ||
            (char*)sqlite3_column_text(getPlayerStandingFromBZIDStmt, 1) != NULL)
        {
            playerStats[1] = (char*)sqlite3_column_text(getPlayerStandingFromBZIDStmt, 0);
            playerStats[0] = (char*)sqlite3_column_text(getPlayerStandingFromBZIDStmt, 1);

            sqlite3_reset(getPlayerStandingFromBZIDStmt);
            return playerStats;
        }
    }

    playerStats[1] = "-1";
    playerStats[0] = "-1";

    sqlite3_reset(getPlayerStandingFromBZIDStmt);
    return playerStats;
}

std::vector<std::string> mofocup::getPlayerStandingFromCallsign(std::string cup, std::string callsign)
{
    /*
        Get the information for a player based on callsign
    */

    std::vector<std::string> playerStats(2);

    sqlite3_bind_text(getPlayerStandingFromCallsignStmt, 1, cup.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(getPlayerStandingFromCallsignStmt, 2, callsign.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(getPlayerStandingFromCallsignStmt, 3, cup.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(getPlayerStandingFromCallsignStmt) == SQLITE_ROW)
    {
        if ((char*)sqlite3_column_text(getPlayerStandingFromCallsignStmt, 0) != NULL ||
            (char*)sqlite3_column_text(getPlayerStandingFromCallsignStmt, 2) != NULL)
        {
            playerStats[1] = (char*)sqlite3_column_text(getPlayerStandingFromCallsignStmt, 0);
            playerStats[0] = (char*)sqlite3_column_text(getPlayerStandingFromCallsignStmt, 2);

            sqlite3_reset(getPlayerStandingFromCallsignStmt);
            return playerStats;
        }
    }

    playerStats[1] = "-1";
    playerStats[0] = "-1";

    sqlite3_reset(getPlayerStandingFromCallsignStmt);
    return playerStats;
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

    //build the query
    sqlite3_bind_text(incrementPointsStmt, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(incrementPointsStmt, 2, cup.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(incrementPointsStmt, 3, pointsToIncrement.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(incrementPointsStmt, 4, cup.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(incrementPointsStmt, 5, bzid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(incrementPointsStmt, 6, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);

    //execute
    sqlite3_step(incrementPointsStmt);
    sqlite3_reset(incrementPointsStmt);
}

bool mofocup::isDigit(std::string myString)
{
    /*
        Check to see if a string is a digit
    */

    for (int i = 0; i < myString.size(); i++) //Go through entire string
    {
        if (!isdigit(myString[i])) //If one character is not a digit, then the string is not a digit
            return false;
    }
    return true; //All characters are digits
}

bool mofocup::isFirstTime(std::string bzid)
{
    /*
        Check if it's the player's first time as part of the current cup
    */

    sqlite3_bind_text(isFirstTimeStmt, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(isFirstTimeStmt, 2, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(isFirstTimeStmt) == SQLITE_ROW)
    {
        sqlite3_reset(isFirstTimeStmt);
        return false;
    }

    sqlite3_reset(isFirstTimeStmt);
    return true;
}

bool mofocup::isPlayerAvailable(std::string bzid)
{
    /*
        Check if a player is on the server based on the BZID
    */

    bz_APIIntList *playerList = bz_newIntList();
    bz_getPlayerIndexList(playerList);

    for (unsigned int i = 0; i < playerList->size(); i++) //Go through all the players
    {
        if (strcmp(bz_getPlayerByIndex(playerList->get(i))->bzID.c_str(), bzid.c_str()) == 0 && bz_getPlayerByIndex(playerList->get(i))->team != eObservers)
        {
            bz_deleteIntList(playerList);
            return true;
        }
    }

    bz_deleteIntList(playerList);
    return false;
}

bool mofocup::isValidPlayerID(int playerID)
{
    bz_APIIntList *playerList = bz_newIntList();
    bz_getPlayerIndexList(playerList);

    for (unsigned int i = 0; i < playerList->size(); i++) //Go through all the players
    {
        if (playerList->get(i) == playerID)
        {
            bz_deleteIntList(playerList);
            return true;
        }
    }

    bz_deleteIntList(playerList);
    return false;
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

sqlite3_stmt* mofocup::prepareQuery(std::string sql)
{
    /*
        Thanks to blast for this function
    */

    // Search our std::map for this statement
    PreparedStatementMap::iterator itr = preparedStatements.find(sql);

    // If it doesn't exist, create it
    if (itr == preparedStatements.end())
    {
        sqlite3_stmt* newStatement;

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &newStatement, 0) != SQLITE_OK)
        {
            bz_debugMessagef(2, "DEBUG :: MoFo Cup :: SQLite :: Failed to generate prepared statement for '%s' :: Error #%i: %s", sql.c_str(), sqlite3_errcode(db), sqlite3_errmsg(db));
            return NULL;
        }
        else
        {
            preparedStatements[sql] = newStatement;
        }
    }

    return preparedStatements[sql];
}

void mofocup::startCup(void)
{
    addCurrentPlayingTimeStmt = prepareQuery("UPDATE `Players` SET `PlayingTime` = (SELECT `PlayingTime` FROM `Players` WHERE `BZID` = ?) + ? WHERE `BZID` = ? AND `CupID` = (SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`)");
    getPlayerInCupStandingStmt = prepareQuery("SELECT `Players`.`Callsign`, `Points`.`Ratio`, `Players`.`BZID` FROM `Points`, `Players` WHERE `Players`.`BZID` = `Points`.`BZID` AND `CupType` = ? AND `Points`.`CupID` = (SELECT `Cups`.`CupID` FROM `Cups` WHERE `Points`.`CupType` = ? AND `Cups`.`ServerID` = ? AND strftime('%s','now') < `Cups`.`EndTime` AND strftime('%s','now') > `Cups`.`StartTime`) ORDER BY `Points`.`Ratio` DESC, `Players`.`PlayingTime` ASC LIMIT 1 OFFSET ?");
    getPlayerStandingFromBZIDStmt = prepareQuery("SELECT `Ratio`, (SELECT COUNT(*) FROM `Points` AS c2 WHERE c2.Ratio > c1.Ratio AND `CupType` = ?) + 1 AS row_Num FROM `Points` AS c1 WHERE `BZID` = ? AND `CupType` = ?");
    getPlayerStandingFromCallsignStmt = prepareQuery("SELECT `Ratio`, `BZID` AS myBZID, (SELECT COUNT(*) FROM `Points` AS c2 WHERE c2.Ratio > c1.Ratio AND `CupType` = ?) + 1 AS rowNum FROM `Points` AS c1 WHERE (SELECT `Callsign` FROM `Players` WHERE `BZID` = myBZID) LIKE ? AND `CupType` = ?");
    isFirstTimeStmt = prepareQuery("SELECT `PlayingTime` FROM `Players` WHERE `BZID` = ? AND `CupID` = (SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`)");
    incrementPointsStmt = prepareQuery("UPDATE `Points` SET `Points` = (SELECT `Points` FROM `Points` WHERE `BZID` = ? AND `CupType` = ?) + ? WHERE `CupType` = ? AND `BZID` = ? AND `CupID` = (SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`)");
    getCurrentPlayerStatsStmt = prepareQuery("SELECT `Points`.`Points`, `Players`.`PlayingTime`, `Points`.`Ratio` FROM `Points`, `Players` WHERE `Players`.`BZID` = `Points`.`BZID` AND `Points`.`BZID` = ? AND `CupType` = ? AND `Points`.`CupID` = (SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`)");
    updatePlayerRatioStmt = prepareQuery("UPDATE `Points` SET `Ratio` = ? WHERE `CupType` = ? AND `BZID` = ? AND `CupID` = (SELECT `CupID` FROM `Cups` WHERE `ServerID` = ? AND strftime('%s', 'now') < `EndTime` AND strftime('%s', 'now') > `StartTime`)");

    bz_APIIntList *playerList = bz_newIntList();
    bz_getPlayerIndexList(playerList);

    for (unsigned int i = 0; i < playerList->size(); i++) //Go through all the players
    {
        std::string bzid = bz_getPlayerByIndex(playerList->get(i))->bzID.c_str(),
                    callsign = bz_getPlayerByIndex(playerList->get(i))->callsign.c_str();

        if (bzid.empty() || bz_getPlayerByIndex(playerList->get(i))->team == eObservers) //don't do anything if the player is an observer or is not registered
            continue;

        if (isFirstTime(bzid)) //introduce players into the MoFo Cup
        {
            bz_sendTextMessagef(BZ_SERVER, playerList->get(i), "Welcome %s! By playing on Apocalypse, you have been entered to this month's MoFo Cup.", callsign.c_str());
            bz_sendTextMessagef(BZ_SERVER, playerList->get(i), "The MoFo Cup is a monthly tournament that consists of the most Bounty, CTF, Geno hits, and kills a player has made.");
            bz_sendTextMessagef(BZ_SERVER, playerList->get(i), "Type '/help cup' for more information about the MoFo Cup!");

            for (int i = 0; i < sizeof(cups)/sizeof(std::string); i++) //Add players to the database for the first time playing
            {
                doQuery("INSERT OR IGNORE INTO `Points` VALUES ('" + cups[i] + "', " + bzid + ", (SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + bz_getPublicAddr().c_str() + "' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 0, 0)");
            }

            doQuery("INSERT OR IGNORE INTO `Players` VALUES (" + bzid + ", '" + callsign + "', (SELECT `CupID` FROM `Cups` WHERE `ServerID` = '" + bz_getPublicAddr().c_str() + "' AND strftime('%s','now') < `EndTime` AND strftime('%s','now') > `StartTime`), 1)");
        }

        bz_debugMessagef(2, "DEBUG :: MoFo Cup :: %s (%s) has started to play, now recording playing time.", callsign.c_str(), bzid.c_str());
        trackNewPlayingTime(bzid);
    }

    bz_deleteIntList(playerList);

    if (addCurrentPlayingTimeStmt == NULL || getPlayerInCupStandingStmt == NULL || getPlayerStandingFromBZIDStmt == NULL ||
        getPlayerStandingFromCallsignStmt == NULL || isFirstTimeStmt == NULL || incrementPointsStmt == NULL ||
        getCurrentPlayerStatsStmt == NULL || updatePlayerRatioStmt == NULL)
        bz_unloadPlugin(Name());
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
        int points, playingTime, oldRank, newRank;

        sqlite3_bind_text(getCurrentPlayerStatsStmt, 1, bzid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(getCurrentPlayerStatsStmt, 2, cups[i].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(getCurrentPlayerStatsStmt, 3, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(getCurrentPlayerStatsStmt);

        if ((char*)sqlite3_column_text(getCurrentPlayerStatsStmt, 0) == NULL ||
            (char*)sqlite3_column_text(getCurrentPlayerStatsStmt, 1) == NULL ||
            (char*)sqlite3_column_text(getCurrentPlayerStatsStmt, 2) == NULL)
        {
            bz_debugMessagef(0, "DEBUG :: MoFo Cup :: An unknown error has occured! A BZID has been returned as NULL");
            sqlite3_reset(getCurrentPlayerStatsStmt);
            return;
        }

        //store the values needed
        points = atoi((char*)sqlite3_column_text(getCurrentPlayerStatsStmt, 0));
        playingTime = atoi((char*)sqlite3_column_text(getCurrentPlayerStatsStmt, 1));
        oldRank = atoi((char*)sqlite3_column_text(getCurrentPlayerStatsStmt, 2));

        bz_debugMessagef(4, "DEBUG :: MoFo Cup :: %s Stats for BZID %s", cups[i].c_str(), bzid.c_str());
        bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   Points        -> %i", points);
        bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   Playing Time  -> %i", playingTime);
        bz_debugMessagef(4, "DEBUG :: MoFo Cup ::   Old Ratio     -> %i", oldRank);

        sqlite3_reset(getCurrentPlayerStatsStmt);

        //calculate the new ratio
        newRankDecimal = (float)points/(float)((float)playingTime/86400.0);
        newRank = int(newRankDecimal);

        bz_debugMessagef(4, "DEBUG :: MoFo Cup :: New ratio for BZID %s -> %i ~= %f", bzid.c_str(), newRank, newRankDecimal);

        sqlite3_bind_text(updatePlayerRatioStmt, 1, convertToString(newRank).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updatePlayerRatioStmt, 2, cups[i].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updatePlayerRatioStmt, 3, bzid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updatePlayerRatioStmt, 4, bz_getPublicAddr().c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(updatePlayerRatioStmt) == SQLITE_DONE)
            bz_debugMessagef(4, "DEBUG :: MoFo Cup :: %s ratio updated successfully for BZID %s", cups[i].c_str(), bzid.c_str());
        else
            bz_debugMessagef(4, "DEBUG :: MoFo Cup :: %s ratio updated failed for BZID %s", cups[i].c_str(), bzid.c_str());

        sqlite3_reset(updatePlayerRatioStmt);
    }
}