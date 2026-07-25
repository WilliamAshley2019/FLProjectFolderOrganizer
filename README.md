This project is independent and is not affiliated with Image-Line / FL Studio. This project aims to provide a tool to help manage external elements such as samples, plugins and .flp files for project management for organization and analysis, extraction, logging and tracking or other workflows not currently user accessible or expedient within FL Studio AFAIK. If you have issues with this project contact me directly at contact@williamashley.music do not contact ImageLine if you experience issues with this project it is not affiliate with them. 

WARNING: None of this is known to be correct, it is purely testing and experimental. Bugs still exist! However, some basic stuff is working. FLP Toolkit is now working however likely bugs not sure what tracks and playlist operations may not be correct not sure.  Although, I am actually very suprised the parsing logic seems to stay compatible for these basic functions through all versions of FL atleast for the limited test items.

2026-07-25 automation tools slight improvement to automation detection has been updated to graphically represent the automation. more testing and refinement is needed but I think I am much closer to getting it working. 

TO DO 
1. I need to figure out step data and get all automation types properly represented. EDIT HAHA, so I was a little stupid as it was explained the the MANUAL itself :)

   What is left is likely simple math for min / max steps determining total steps at each tension then correlating the tension ratio to step ratio.
   Pulse at tension 0.0 (center)  

Pulse at tension 1.0 (maximum)  

Pulse at tension -1.0 (minimum) 

Same for Stairs and Wave
```text┌─────────────────────────────────────────────────────────────────────────────────────┐
0x00	Single Curve	Controls curve steepness
0x01	Double Curve	Controls S-curve asymmetry
0x02	Alt Single Curve	Controls curve steepness
0x03	Stairs	Controls step frequency
0x04	Smooth Stairs	Controls step frequency
0x05	Pulse	Controls pulse frequency
0x06	Hold	Tension ignored (maybe)
0x07	Wave	Controls wave frequency
0x08	Half Sine	Controls curve shape
0x09	Single Curve 2	Controls curve steepness
0x0A	Single Curve 3	Controls curve steepness
0x0B	Double Curve 2	Controls S-curve asymmetry
0x0C	Double Curve 3	Controls S-curve asymmetry
└──────────────────────────────────────┴──────────────────────────────────────────────┘
```
3. I need to fixure out a format the automations can be exported as, or create a export file type unique to automations so they can be migrated for other purposes
4. get the sample collection mechanism working for using the sample data base and file info to copy the samples into the working folder of the flp or a designated folder.




2026-07-25 - the project continues hopefully an update correct some errors in automation data assumptions, may have it understood now.  Need to test. Effectively it works just like the UI
behaves. But with a couple nuances as to how the datapoints are shared. It is actually very logical in how the points are ordered. However knowing what and how many bytes each aspect of the line segment is a lot of guesswork.

How automation point data "might work"
Start (slot 0) + point 1's own position (always 0"?") and value. No incoming curve. What I don't know is if automation clips are stored releative to a virtual location are are mapped to the len of any point they are placed, I want to think they are relative to a type of "pattern" that is non linear or a pocket and only each placement of the clip is somehow mapped rather than each automation clip being  mapped directly to the location it occurs with values internally in the line (automation) being value adjusted based on placement, my guess is currently automation clips exist in a pocket seperate from playlist position and the clip itself holds some type of placement information as a container to play the automation within it.
Every point P after that has its position/value stored in slot (P−1)'s last 16 bytes
Its tension/curve-type stored in slot P's first 8 bytes — one slot later. (Regressive mapping (makes sense to plot a line since the last point is not forward directing)
It overlaps the 24 bytes.
End point tension/curve-type lands in one extra trailing slot but the position/value fields are unused (NaN) 

The parser and automation app need to be updated to hopefully get the lines to draw properly. 
I am guessing there is still something between automation events and the actual automation clips that is not accounted for I don't know what it is yet.

How steps within a curve work not 100% sure where that data can be parsed I know it is there I'm just not sure where yet. So I think I am very close to understanding automation clips now.

2026-07-22 - Minor update automatoins are now sort of detecting in the automation tool WARNING, the tool still isn't optimized, I need to figure out exactly what I would like it to do. I will likely like to draw the automation so people can see what they are doing to it. Note there is likely more to automations than what has been solved for however I think I might have the automation types detected so specific automation event type and structure can be done, I havn't correlated the actual automation event /  clip to type match the actual form of the shape yet, this is something I'd like to do just to know I've got it right.

NOTE: NO WRITE FUNCTIONS HAVE BEEN TESTED AT ALL YET. DO NOT USE AN ORIGINAL FLP TO MODIFY AUTOMATIONS UNLESS YOU ARE WILLING TO CORRUPT THAT FILE, AS I DON"T KNOW WHAT TYPE OF ANTIFILE CORRUPTION STUFF FLP files have written into them.

I have yet to attempt to modify data in a flp with this tool, only extract it. It may be some time before I figure this out.  Currently I do not have plugin loading quite right in the write process so DO NOT ATTEMPT TO MODIFY ANY AUTOMATION CURVES YOU WILL LIKELY CORRUPT YOUR PROJECT. However the big milestone is that it now detects the automation IID.   It shoudl not be underestimated how useful figuring out how automations functino partially (I think there is more to it still) was as far as getting past that roadblock. For likely doing more with translating flp files for other purposes as automation data is pretty important to preserve.

TO DO refine detection outputs
add automation visual representation
determine a way to extract automations in some usable way.
??? fix the ability to modify / write to flp files in a way that preserves file integrity???????? not sure about this as there are some issues with facilitating direct file rewrites of some of the data, so need to think about this more.

way more work is needed on refining the automation stuff.
I am not sure when or if I will work on modifying flp files this way as a "rapid editor" via text ui/gui or batch interface. to batch run changes with the flptoolkit but its plausible.

There are some known issues. On like day 10  of my water fast now that I intend to continue till the end of the month feeling great, however  I am pretty blah for doing much even though I feel fine. 

## FLP Automation Structure? Not sure yet.

```text┌─────────────────────────────────────────────────────────────────────────────────────┐
│                           FLP Automation Structure (CONFIRMED)                      │
├──────────────────────────────────────┬──────────────────────────────────────────────┤
│ Legacy Automation                    │ Modern Automation Clip                       │
│ (Pattern-Bound / Pre-FL 12)          │ (Channel-Based / FL 12+)                     │
├──────────────────────────────────────┼──────────────────────────────────────────────┤
│ PatternCtrlsEvent                    │ RemoteControllerEvent (EventID: 227)         │
│ EventID: 223                         │ └── Links automation to target parameter     │
│                                      │     └── trackId → source automation channel  │
│ Controller[]                         │     └── destId → target (mixer/plugin param) │
│ ├── position (uint32)                │     └── paramId → parameter index            │
│ ├── channelIID (uint8)               │                                              │
│ └── value (float)                    │ AutomationEvent (EventID: 234)               │
│                                      │ └── 13-byte header                           │
│                                      │ └── Record[] (24 bytes each)                 │
│                                      │                                              │
│                                      │ ┌──────────────────────────────────────────┐ │
│                                      │ │          Automation Record               │ │
│                                      │ ├──────────────┬───────────────────────────┤ │
│                                      │ │ Offset 0-3   │ controlCode (int32)       │ │
│                                      │ │              │ 0x03 = endpoint           │ │
│                                      │ │              │ 0x00 = interior point     │ │
│                                      │ │              │ -1 = sentinel             │ │
│                                      │ ├──────────────┼───────────────────────────┤ │
│                                      │ │ Offset 4-7   │ curveType (int32)         │ │
│                                      │ │              │ See table below           │ │
│                                      │ ├──────────────┼───────────────────────────┤ │
│                                      │ │ Offset 8-15  │ position (double)         │ │
│                                      │ │              │ PPQ ticks (relative)      │ │
│                                      │ ├──────────────┼───────────────────────────┤ │
│                                      │ │ Offset 16-23 │ value (double)            │ │
│                                      │ │              │ Normalized 0.0-1.0        │ │
│                                      │ └──────────────┴───────────────────────────┘ │
│                                      │                                              │
│                                      │ PluginSettings (binary blob)                 │
│                                      │ └── (Additional plugin-specific data)        │
│                                      │                                              │
│                                      │ ChannelPlaylistItem                          │
│                                      │ └── Position, Length (in PPQ)                │
└──────────────────────────────────────┴──────────────────────────────────────────────┘
```

Best Guesses?  This may not be correct
## Automation Curve Types

| Value | Curve Type | Description |
|------:|------------|-------------|
| `0x00` | Linear | Straight line between points |
| `0x01` | Double Curve | S-curve (ease-in-out) |
| `0x02` | Single Curve | Bézier curve with tension |
| `0x03` | Stairs | Step/quantized curve |
| `0x04` | Smooth Stairs | Smoothed step curve |
| `0x05` | Half Sine | Half sine wave |
| `0x06` | Hold / Pulse | Sample-and-hold / pulse wave |
| `0x07` | Wave | Full sine wave |
| `0x08` | Flat Anchor | Endpoint marker |
| `0x09` | Single Curve 2 | Alternate single-curve variant |
| `0x0A` | Single Curve 3 | Alternate single-curve variant |
| `0x0B` | Double Curve 2 | Alternate double-curve variant |
| `0x0C` | Double Curve 3 | Alternate double-curve variant |

## Control Code Values

| Value | Meaning | Position in Curve |
|------:|---------|-------------------|
| `0x00000003` (`3`) | Endpoint | First point in the automation clip |
| `0x00000000` (`0`) | Interior Point | Middle points (curve segments begin here) |
| `0xFFFFFFFF` (`-1`) | Sentinel | End-of-data marker |

## Interpolation Formula Reference

| Curve Type | Interpolation |
|------------|---------------|
| Linear (`0x00`) | `v = a + (b - a) * t` |
| Single Curve (`0x02`) | `v = smoothstep(a, b, t)` |
| Double Curve (`0x01`) | `v = easeInOut(a, b, t)` |
| Stairs (`0x03`) | `v = (t < 0.5) ? a : b` |
| Smooth Stairs (`0x04`) | `v = smoothstep(a, b, t * t * (3 - 2 * t))` |
| Half Sine (`0x05`) | `v = a + (b - a) * sin(t * π / 2)` |
| Hold (`0x06`) | `v = (t < 0.99) ? a : b` |
| Wave (`0x07`) | `v = a + (b - a) * (sin(t * 2 * π - π / 2) * 0.5 + 0.5)` |
| Flat Anchor (`0x08`) | Endpoint marker (no interpolation) |
| Variants (`0x09`–`0x0C`) | Same as their base curve types with slight variations |

Binary Compatibility: Delphi's record layout and packing rules influenced how FL Studio serializes data to disk. 
24-byte automation Delphi's packed record behavior.

Double Precision: Delphi's Double type (8 bytes, IEEE 754), double at offset 8 and 16.
Type-length-value encoded events and structs

event stream - list of {type, data} pairs similar to MIDI files.
Events are sequential (no random access)
New features = new event types.
Old events ? ignored?


None of this is known as I don't know what the actual internal structures are inside FL Studio, this is all "guesswork" however it is sort of starting to detect there are likely still tons of bugs and untapped information in there to accurately render the automations, its time intensive to do this so this project will likely move slow

2026-07-20 after a fast week of doing an extended water / tea fast which is continuing there was some slight progress on the project. Added .json export of some project data. 
Some of the plugin and sample stuff is working a little better to include audio clips however its still not 100% I will likely need to cross link the plugin database because I am detecting
the plugin "title" rather than the plugin type for some synth instruments. This will likely be ironed out with fairly simple cross linking of data not 100% sure the structure yet likely
Plugin -> preset/title. The audio clips were a little weird likely cause they were added into fl studio later and have a particular strucutre that exists in a special way between channel
rack and playlist.  A bunch of other things will update in more detail likely. https://github.com/WilliamAshley2019/FLProjectFolderOrganizer/tree/main/FLPTOOL
I have a sense there is so much stuff that could be going on in a flp that it will not be a quick victory to get everything detecting and working properly however some basic key functions seem to be working now. 

TO do: figure out how automation clip points work so automation data can be properly extracted. Automation events and automation clips have evolved so wrapping my head around how the data is housed in the flp will need a little attention. Adding links to more useful tools https://github.com/monadgroup/FLParser

I thought I might as well start documenting some background info regarding solutions.  Its also important to note that the flp format seems to more or less be stable but versions added features as new functions within fl studio were added. I havn't quite wrapped my head around these, but I think that automations will wwork in the next update. Current assumption is values stride-24 with the value offset 16, not 100% sure on this testing.  Automation Curves arn't quite solved for yet.  I will see if I can find this specific info somewhere online.

2026-07-12 started to resolve the plugin detection code to attempt to read fl native plugins correct etc. Part of that bug was that internal engine plugins such as TS404 and drumsynth may have not been in the same type of wrapper so they were detecting as unknown. FL format has been considered as a way of detecting fruity plugins rather than using more indirect methods such as using plugin names themselves, this can cross index plugin database information to get a correct idea of what plugins are.   More needs to be done but should hopefully be in place by the next major update to the source. Started to work on midi extraction.  Also started trying to merge the FLProjectOrganizer and FLPTOOLKIT into a common codebase that can use the code from the other in hopes of making FLPtoolkit an extension of FLProjectOrganizer.  Big success in this one is getting the UI to work and successfully export detected midi patterns as .mid. Not entirely sure how working it is yet though.

To do, build out UI functions for processes that are possible but not accessible via UI.
To do,  expand the midi export function where possible to somehow link the presets/plugins with a linking file that can be used to at some point not only load the .mid but also serve as a way of knowing what midid files load what presets or plugins and associated data. This will be very useful if sample names etc. may be better maintained in the midi export so flex might load the closest samples to the exported, if there is someway of crosslinking sample names or types with flex midi loads or loading the actual presets/plugins and samples with the midi drop not 100% sure how that is doable external to fl studio yet.


CURRENTLY THIS PROJECT IS SEPERATED INTO TWO WORKING PROJECTS FLProjectOrganizer and FLPTOOL (flp toolkit)   these projects intend to be interlinked with flptoolkit allowing deeper functions from analysis and possible batch translation of data such as midi extraction or ways of working with flp as data structures rather than a whole file. I don't actually know the flp format I have not done any reverse engineering I am just applying the logic of the other projects on github like Katai and PyFLP to try to translate and connect their work into a C++ / Juce 8 form.  This tool is intended to complement fl studios functions to allow better organization and operations like sample and plugin management in ways that currently are not possible from within fl studio however it is very possible this tool will not grow to a point it is usable on modern versions of flp due as its not clear if any data in those  files has been opened by any other open source projects.

2026-07-10 FLP TOOLKIT is now getting the test data mostly for early versions of frutiyloops, tested on a couple old files. I think maybe the search and numbers now lines up  based on a couple old file tests still some quirks however the basic milestone is functioning more testing is needed to see exactly the cuttoff point of the parsing logic for the flp format. There may be some structures that are not supported it seems the parsing logic holds up to quite recently. 

to do, tie in FLProjectFolderOrganizer  version source check into switching modes for parsing logics based on flp format parsing logic changes. I suspect this will get far more difficult to get right with each new version due to increasing binary and funcitions within fl studio, I likely will not be able to test all these myself due to the time of testing all fl studio flp
project calls and functions - as it is likely hundreds of functions to account for, as the project is already thousands of lines of code I suspect an effective tool to analyze flp 
project data may be a little beyond my ability to manage to the level I would like but I will still try to progress this project and the FLProjectFolder Organizer tools. This project might start to make more sense as a standalone app as it grows in size though.Time will tell. Yet success on the flp toolkit I can read basic info like missing samples. 

To do tie in the missing samples data to direct sample search and cross referencing of the scanned sample database to locate the missing samples.  This may expand the search path to the total scanned database not simply the fl studio search folders. Also it is possible to do an add in to tie in a broader search such as internet search etc.. in the future automating search functions to external tools/apps to locate the missing samples.

issues: ts404 detects as 3rd party tool, not sure why. channel rack tracks and playlist patterns do not quite detect possibly still some buggy stuff with track detection not sure why yet.
Not sure the fruityloops version cutoff for analysis however newer flps read as corrupt likely due to binary format not allowing access to data entry points that are able to be parsed? 32 vs 64 bit data structures perhaps?

2026-07-08 On load file is TOTALLY not parsing properly as of yet so super buggy, I do think it is trying to pull data but I am thinking it is not doing a good job of it at this point. 100% will need some attention to sort out what data it is actually pulling. Debug may need apply.

To DO the challenges at this point are 1. insuring it can open all FLPs to gather the needed data 2. add a "flp scan" to the sample scan section. Enable the ability to copy samples from flp to selected folder or create project folder and copy flp and samples to new project folder.  3. Add a vst scan section for the plugin manager to do a "manage plugins per project.  4. batch processes are more complex to run project bones for instance via batch process. I am sure there are other useful functions. Actual FLP operations remain "occulted" as I don't fully understand the hash functions I am guessing may be involved to prevent FLP corrupt so reading is one thing, writting to or editing flp files is another matter. The real issue is to insure that specific secuity functions are not tampered with, nor the ability to tamper with them included in this project.


I will start referencing sources drawn from however they may not in fact be in the code verbatim as some of the code was adapted from python projects.  I will start listing projects drawn from https://github.com/demberto/PyFLP  (GPLv3)   other projects listed at #demberto's github https://github.com/demberto/flp.ksy   https://github.com/demberto/FLPInfo !!!  thanks to #Demberto for providing all this useful information to the public. there are likely others I referenced I just need to figure out what they were, again not sure if any source survives but they were 100% referenced as no actual reverse engineering was done on this project.

2026-07-08 FLP Toolkit compiles and is found on vst3 scan, loads into flstudio mixer effect slot. It can open SOME flp files, I am thinking perhaps either the size of the file or VERSION of the flp file determines whether it loads or not I am troubleshooting this it gets basic  info but I am not sure it is implemented properly yet. However this is atleast the compile and load flp milestone met. Still not getting much useful info. It loaded an old flp from 2001 however it failed to load a newer FLP file I am going to need time to figure the specific thing causing the creash whether it is the file buffer as no error screen or crash screen popped up in fl studio it just closed so no info on the cause of the crash yet. So this is unstable on load flp but loading as a barebones plugin, some info is determined but I'm not sure if it is correct yet more testing is required to determine if the assignment info used was correct.

2026-07-07 working on flp file tool that is being built as a standalone tool but intended to merge with this project as a a new flptool optimistic it will provide a lot more access to info and stuff in flp files.  Currently just building out the source for this sort of what the first attempt to get the code up and what it will handle is up but it still has bugs it is getting closer to compiling thoug. however due to the various versions of flp I really have no idea if the code I drew from was correct or not. NOTE I will likely get some licenses up for the code once I get it compiled there probably are people who should get mention I just havn't finalized the code yet so I am not sure who 100% will be licensed as the source as there were some overlapping data points and it was all adapted and translated from other language to C++.

Must remember to move plugin database to something like  juce::ThreadPool loaderThread { 1 }; // 1 background thread

To add  
├── flp.h                 (Core parser header + added patches)
├── flp.cpp               (Core parser implementation + added patches)
├── flphelper.h           (All 9 utility modules combined)  - plugin scanner, sample scanner, arranger, comparer, cleaner, batch processor, midi bridge, automation editor and statistics generator
├── flphelper.cpp         (All 9 utility modules combined)




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

