VERSION 5: changes - this version is a little unstable with plugin database launch - this is known and being fixed hopefully for the next update, plugin database will load but takes longer with more plugins. If you click cancel rather than end task it should load, and it loads in a seperate popup window.  
1. the addition of plugin management was started in the plugin manager allowing the removal of plugins from the database - this is not yet clean
2. version number of the plugins are provided if available via the PE call, this should be a helpful tool to know the version number of the plugin you are using
3. corrected the sort order for each column in plugin manager now sorts alphabetically and including type/groups
4. implemented "delete' type in for confirmation of destructive processes with plugin database to insure incorrect selections are not removed in error. 
   
To add
   f. the ability to read and write data with unmp3 or its associated files.  to read and write sample data - incorporating not 100% sure yet UNMp3 with the plugin, this will likely be working with the .remeta file.
    g. audio is going into the plugin but not audio output, need to bypass audio processing, as it may stop audio playback if loaded on a live channel, this may be confusing to people not aware of why that is happening.
   h. plugin processes need to be moved off the process thread. current database launch on large databaes takes far too long to populate resulting in a hang that will resolve with time however it can hange fl studio making it unstable
   i. other things are also to be moved off the message thread.
   j. as part of this backing up database states as part of the load process will reduce the amount of data needing to be repopulated from process calls, the ability to rescan or update will be through a new button.
   


VERSION 4: changes
1. more safety mechanisms were added to reduce the risk of deleting files by accident, read only and system files now get filterd to a "protected folder" still need to test this, not sure how to yet. Likely need to make some read only files for testing purposes and maybe duplicated a system file or something. Still buggy. only really practical for cleaning.
2. added the ability to edit some plugin entries, still limited. need to test if it effects what catagories the plugins show up in, it will likely be reset on fl studio rescan

   TO ADD:
   a. include that export as csv will keep all atrritubes and "replace attributes" from the .csv on csv import so that settings such as type can be reset.
   b. the ability to have the "plugin version number"  displayed if possible so you know the specific build of the plugin if it is accessible via vst host infos.
   c. the  option to right click "delete entry and plugin." so the plugin entry in the data base .nfo / .fst and associated vst file/folder is also deleted with right click interaction
   d. clicking on the "type column in plugin manager lets you change from synth to effect to synth or unkown?
   e. sorting by alpha betic when clicking on column header  so you can group plugins by the same vendor, or by plugin type or by catagory, or alphabetic plugin name

   
-----------------------------------------------------------------
VERSION 3
WARNING. this is test only do not USE DELETE function it may be collating non .flp files still and if move function may move then to a different folder which for c:\ etc.. may move important files to a folder that will NOT be good... so do not use this currently to delete files if you are scanning adirectory with files that should not be deleted or moved, I still need to test how this is working wihtout testing it on my own system ... cause it might delete files I need so I will likely block off files if they are system files etc.. and stuffages. 


Version 3 added - basic plugin manager scan - intent to add editing capability for trouble shooting however quite basic at this point. 
to do still add .wav and .mp3 scan to create a master usable database for samples and mp3 etc.. files, and ability to interact with the for unmp3 . remeta analysis and
metadata extractio and tagging. 

to do add script functions to powershell to run some scripts to organize or do specific useful functions with the database. 
bugs to fix - the tabs are not collating alphanumerically. 
not able to switch version from synth to effect via right click currnetly read only mode, need to change database entries to write / edit mode so incorrect assignment
bug for synth / effect can be easily fixed.

bug - files are being added to scan as "unknown" when not flp not good if someone adds an important directory, need to block 

(note the project showing up as C is due to the size of the sqlite.c file which is massive compared to the rest of the code it is indeed a juce C++ project wtih only the one .c file for the sqlite stuff.
-----------------------------------------------------------------

VERSION 2 (second time typing this as second power outage this morning just struck while typing): what is new

1. right click interfaces to open in a specific version of FL or the project folder
2. added fl1 and fl2 support / detection that wasn't added in the original verison
3. moved hash display off the main display
4. removed "Studio" from plugin name to avoid any direct branding confusion
5. added recycling bin to insure no default file deletions, extra protection layer to accidental unrecoverable file deletions located in documents/WAM folder created context of WAM folder
6.  hardrive space check to prevent operations that cannot complete due to space restrictions
7.  moved database from appdata to /documents/WAM for easier access

To Add: 
1. move flp to project folders rather than loose
2. update v. number :) to 1.0.1
3. add plugin database interaction
4. add sample .wav/.mp3 interaction with tie in to UNMP3 functions such as sample analysis and metadata modification
-----------------------------------------------------------------
VERSION 1
This is an FL organization tool for managing FL Studio projects and related audio files. It originated from a JUCE-based prototype implemented as a VST3 effect that can also run standalone; the JUCE project includes file operations and hashing utilities and may be a useful base for adding database and file-management features.


Build info - JUCE 8.0.12 may work standalone havn't tested it.  (this requires a few extra modules beyond plugin basics - such as the cryptographic module.. if you encounter build errors try throwing in a few more modules :) 100% remember that one though.  There are hashing functions - in fact this juce project does quite a bit of file operations in general and may be a nice base to add database operations and file management functions for other purposes.


used visual studio 2026 to compile

only tested on windows.

works

set target search path - it could scan an entire drive or a folder .. it will search everthing to see if it it is a fl studio project file and its version
this is the scope of its search - although it might provide a good base to do some other type of database search operations such as presets - then searching for presets in the projects
move them into the project folder located samples if listed etc.. perahps something to add tomorrow. 

you can export the database to a csv file this gives you a database of all fl studio projects and their version made with and a few other data points - this could be useful
for other purposes such as data base management, determining project creation dates for rights administration, project management or whatever there are likely uses.

you can copy  the files to a newfolder orgaization  I made it so that it would move flp projects made with a version to a folder dedicated to that version on the major release number

I should add fl 2 and fl to the list currently starts at fl3
TO ADD function to open projects from the plugin itself in a the "version" fl studio it was made in??? link to fl64 exe or other launch .exe version ... should try to add this
transforms it from a search and organize tool to a proper file management system.
there is an option to delete the original files after moving but I left this something you had to click to enable cause it could delete your files perhaps if somethign weird happened.

TO add maybe a double confirmation to delete as one layer of protection had me think someone is going to mess this up and delete all their flp files.

None the less have a good day, if there are any features you would like added or if you spot anybugs 

contact   contact@williamashley.music

I will update this readme one the project has perculated a little.. it is the end of the day for me so this is the end of the project for the time being.

I think this is a great start to a good database managmement too for FL - it is GPLv3 so it is able to be adapted

If you do adapt this please notify me so I can check out your project and see what you are doing with it.


consider expanding this to .fst or other file types used by fl studio to help organize and find those also... need to consider way of correlating fst type to plugin type likely in the fst itself  

could extend this to presets in general need to consider

.wav search etc.. functions

coordiation of UNMP3 side attachment??? for file management operations and  .remeta extraction and writes to files or id3 management of .mp3s in the database  perhaps open additional side tab or something to display the metadata information files or any useful information from files clicked???? 


NOTE currently possible issue with filespace on drive, not sure resource issues with fl itself but could exist... likely not IDEAL to keep loaded in projects when not in use???

ability to delete entries?? or right click specific file operations??? 
Attribution for the hours of hard work I put into overseeing this project developed are always appreciated.  :) (this was made in one waking day 2026-06-29) 


However the line about doing good and sharing etc.. and all that other hippy stuff from Dr. Hipp is pretty nice too.

