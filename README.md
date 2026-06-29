it is a vst3 effect that is simply a way I chose to build it , it likely works standalone also as it was made to work standalone originally but thought it would be intersting if I had tools
able to be loaded as vst3 plugins rather than programs loaded outside of the program.


Build info - JUCE 8.0.12 may work standalone havn't tested it.  (this requires a few extra modules beyond plugin basics - such as the cryptographic module.. if you encounter build errors try throwing in a few more modules :) 100% remember that one though.
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

there is an option to delete the original files after moving but I left this something you had to click to enable cause it could delete your files perhaps if somethign weird happened.

TO add maybe a double confirmation to delete as one layer of protection had me think someone is going to mess this up and delete all their flp files.

None the less have a good day, if there are any features you would like added or if you spot anybugs 

contact   contact@williamashley.music

I will update this readme one the project has perculated a little.. it is the end of the day for me so this is the end of the project for the time being.

I think this is a great start to a good database managmement too for FL - it is GPLv3 so it is able to be adapted

If you do adapt this please notify me so I can check out your project and see what you are doing with it.

Attribution for the hours of hard work I put into overseeing this project developed are always appreciated.


However the line about doing good and sharing etc.. and all that other hippy stuff from Dr. Hipp is pretty nice too.

