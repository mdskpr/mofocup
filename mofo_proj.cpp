#include "mofo_proj.h"

typedef 
{
 int player_bzID;
 int cap_num;
}CapList;

class CapNRecEvents : public bz_Plugin
{
  public:
       virtual void Init(const char*);
       virtual void Event(bz_eventData);
}

virtual void Init(const char*)
{
     Register(bz_eCaptureEvent);
}


virtual void Event(bz_eventData *eventData)
{
	ScorerDef capper;
	ofstream data_file;
	data_file = fopen("cap_record.txt", "w+");
	int lineno;
	switch(eventData->eventType){
	    case bz_eCaptureEvent:
	       bz_CaptureEventData_V1* data = (bz_CaptureEventData_V1*)eventData;
	       bz_BasePlayerRecord *player = bz_getPlayerByIndex(data.playerCapping);
	       capper.player_bzID = player.bzID;
	       if(doesPlayerDataExist(capper.player_bzID, data_file)){
	          string capper_line;
	          capper.cap_num++;
		  while(!data_file.eof()) {
		    //need to figure out how to overwrite cap number for that player.
	          } 
	       } else {
	          capper.cap_num++;
	          data_file  << capper.player_bzID << ";" << "\0" << capper.cap_num << "\n";

	       }
	       

 	       

	}
	

}

bool doesPlayerDataExist(int player_bzID, ofstream data_file){
    while(!data_file.eof()) {
       string sLine = ";";
       string each_line;
       each_line = getline(data_file, sLine);
       if(strcmp(player_bzid, each_line) == true){
        return true;
       }
    }
    return false;
}

void readCapList(void){
     std::string line;
     std::ifstream myCapFile("cap_record.txt");
     int tmp = 0;

     CapList myCapListData;

     myCapList.clear();

     if(myCapList.is_open()) {

        while(myCapList.good()) {
	  
	  getline(myCapFile, line);
	  tmp++;
	  getline(line,myCapList.player_bzid, ';');
	  myCapListData.cap_num = line.substr(myCapListData.player_bzid.size() + 2, line.size()); 
	  myCapList.push_back(myCapListData);

	}
        myCapFile.close();
     }
}


BZ_PLUGIN(CapNRecEvents);

