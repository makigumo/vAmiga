// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#pragma once

#include "SubComponent.h"
#include "Interpreter.h"
#include "RemoteServer.h"
#include "TextStorage.h"

#include <sstream>
#include <fstream>

class RetroShell : public SubComponent {

    friend class RemoteServer;

public:

    // Server for managing remote connections
    RemoteServer remoteServer = RemoteServer(amiga);

private:
    
    // Interpreter for commands typed into the console window
    Interpreter interpreter;

    
    //
    // Text storage
    //

private:
    
    // The text storage
    TextStorage storage;
 
    // History buffer storing old input strings and cursor positions
    std::vector<std::pair<string,isize>> history;
    
    // The currently active input string
    isize ipos = 0;

    // Wake up cycle for interrupted scripts
    Cycle wakeUp = INT64_MAX;

public:
    
    // Indicates if TAB was the most recently pressed key
    bool tabPressed = false;
        
    
    //
    // User input
    //
    
    // Input line
    string input;
    
    // Input prompt
    string prompt = "vAmiga% ";

    // Cursor position
    isize cursor = 0;
    
    
    //
    // Scripts
    //
    
    // The currently processed script
    std::stringstream script;
    
    // The script line counter (first line = 1)
    isize scriptLine = 0;

    
    //
    // Initializing
    //
    
public:
    
    RetroShell(Amiga& ref);
        
    // Returns the welcome message
    std::stringstream welcome() const;
    
    // Dumps the current text storage to the remote server
    void dumpToServer();
    
    
    //
    // Methods from AmigaObject
    //
    
private:
    
    const char *getDescription() const override { return "RetroShell"; }
    void _dump(dump::Category category, std::ostream& os) const override { }
    
    
    //
    // Methods from AmigaComponent
    //
    
private:
    
    void _reset(bool hard) override { }
    
    isize _size() override { return 0; }
    u64 _checksum() override { return 0; }
    isize _load(const u8 *buffer) override {return 0; }
    isize _save(u8 *buffer) override { return 0; }


    //
    // Working with the text storage
    //

public:
    
    // Returns the contents of the whole storage as a single C string
    const char *text();
        
    // Moves the cursor forward to a certain column
    void tab(isize pos);

    // Prints a message
    RetroShell &operator<<(char value);
    RetroShell &operator<<(const string &value);
    RetroShell &operator<<(int value);
    RetroShell &operator<<(long value);
    RetroShell &operator<<(std::stringstream &stream);

private:
    
    // Clears the console window
    void clear();
    
    // Prints a help line
    void printHelp();
    
    // Clears the current line
    void clearLine() { *this << '\r'; }
    
    
    //
    // Managing user input
    //

public:

    isize inputLength() { return (isize)input.length(); }
    
    void pressUp();
    void pressDown();
    void pressLeft();
    void pressRight();
    void pressHome();
    void pressEnd();
    void pressTab();
    void pressBackspace();
    void pressDelete();
    void pressReturn();
    void press(char c);
    void press(const string &s);
    
    // Returns the cursor position relative to the line end
    isize cursorRel();
    

    //
    // Working with the history buffer
    //

public:
    
    isize historyLength() { return (isize)history.size(); }

        
    
    //
    // Executing commands
    //
    
public:
    
    // Main entry point for executing commands that were typed in by the user
    void execUserCommand(const string &command);

    // Executes a command
    void exec(const string &command) throws;
    
    // Executes a user script
    void execScript(std::ifstream &fs) throws;
    void execScript(const string &contents) throws;

    // Continues a previously interrupted script
    void continueScript() throws;

    // Prints a textual description of an error in the console
    void describe(const std::exception &exception);

    // Prints help messages for a given command string
    void help(const string &command);
    
    
    //
    // Command handlers
    //
    
public:
    
    // void handler(const string& command) throws;
    
    template <Token t1> void exec(Arguments& argv, long param) throws;
    template <Token t1, Token t2> void exec(Arguments& argv, long param) throws;
    template <Token t1, Token t2, Token t3> void exec(Arguments& argv, long param) throws;

private:
    
    void dump(AmigaComponent &component, dump::Category category);

    
    //
    // Performing periodic events
    //
    
public:
    
    void vsyncHandler();
};
