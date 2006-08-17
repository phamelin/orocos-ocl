
/**
 * This file demonstrate each Orocos primitive with
 * a 'hello world' example.
 */

#include <rtt/os/main.h>

#include <rtt/GenericTaskContext.hpp>
#include <taskbrowser/TaskBrowser.hpp>
#include <rtt/Logger.hpp>
#include <rtt/Property.hpp>
#include <rtt/Attribute.hpp>
#include <rtt/Method.hpp>
#include <rtt/Command.hpp>
#include <rtt/Event.hpp>
#include <rtt/Ports.hpp>
#include <rtt/PeriodicActivity.hpp>

using namespace std;
using namespace RTT;
using namespace Orocos;

/**
 * Every component inherits from 'TaskContext'
 * or from 'GenericTaskContext'. These base classes
 * allow a user to add a primitive to the interface
 * and contain an ExecutionEngine which executes
 * application code.
 */
class HelloWorld
    : public TaskContext
{
    /**
     * Name-Value parameters:
     */
    /**
     * Properties take a name, a value and a description
     * and are suitable for XML.
     */
    Property<std::string> property;
    /**
     * Attributes take a name and contain changing values.
     */
    Attribute<std::string> attribute;
    /**
     * Constants take a name and contain a constant value.
     */
    Constant<std::string> constant;

    /**
     * Input-Output ports:
     */
    /**
     * DataPorts share data among readers and writers.
     * A reader always reads the most recent data.
     */
    WriteDataPort<std::string> dataport;
    /**
     * BufferPorts buffer data among readers and writers.
     * A reader reads the data in a FIFO way.
     */
    BufferPort<std::string> bufferport;

    /**
     * Methods:
     */
    /**
     * Methods take a number of arguments and
     * return a value. The are executed in the
     * thread of the caller.
     */
    Method<std::string(void)> method;

    /**
     * The method function is executed by
     * the method object:
     */
    std::string mymethod() {
        return "Hello World";
    }

    /**
     * Commands:
     */
    /**
     * Commands take a number of arguments and
     * return true or false. They are asynchronous
     * and executed in the thread of the receiver.
     */
    Command<bool(std::string)> command;

    /**
     * The command function executed by the receiver.
     */
    bool mycommand(std::string arg) {
        log() << "Hello Command: "<< arg << endlog();
        if ( arg == "World" )
            return true;
        else
            return false;
    }

    /**
     * The completion condition checked by the sender.
     */
    bool mycomplete(std::string arg) {
        log() << "Checking Command: "<< arg <<endlog();
        return true;
    }

    /**
     * Events:
     */
    /**
     * The event takes a payload which is distributed
     * to anonymous receivers. Distribution can happen
     * synchronous and asynchronous.
     */
    Event<void(std::string)> event;

    /**
     * Stores the connection between 'event' and 'mycallback'.
     */
    Handle h;

    /**
     * An event callback (or subscriber) function.
     */
    void mycallback( std::string data )
    {
        log() << "Receiving Event: " << data << endlog();
    }
public:
    /**
     * This example sets the interface up in the Constructor
     * of the component.
     */
    HelloWorld(std::string name)
        : TaskContext(name),
          property("Property", "Hello World Description", "Hello World"),
          attribute("Attribute", "Hello World"),
          constant("Constant", "Hello World"),
          dataport("DataPort","World"),
          bufferport("BufferPort",13, "World"),
          method("Method", &HelloWorld::mymethod, this),
          command("Command", &HelloWorld::mycommand, &HelloWorld::mycomplete, this),
          event("Event")
    {
        // Check if all initialisation was ok:
        assert( property.ready() );
        assert( attribute.ready() );
        assert( constant.ready() );
        assert( method.ready() );
        assert( command.ready() );
        assert( event.ready() );

        // Now add it to the interface:
        this->properties()->addProperty(&property);

        this->attributes()->addAttribute(&attribute);
        this->attributes()->addConstant(&constant);

        this->ports()->addPort(&dataport);
        this->ports()->addPort(&bufferport);

        this->methods()->addMethod(&method, "Hello Method Description");
        
        this->commands()->addCommand(&command, "Hello Command Description",
                                     "Hello", "Use 'World' as argument to make the command succeed.");

        this->events()->addEvent(&event, "Hello Event Description",
                                 "Hello", "The data of this event.");

        // Adding an asynchronous callback:
        h = this->events()->setupConnection("Event").callback(this, &HelloWorld::mycallback, this->engine()->events() ).handle();
        h.connect();
    }
};

int ORO_main(int argc, char** argv)
{
    Logger::In in("main()");

    // Set log level more verbose than default,
    // such that we can see output :
    if ( log().getLogLevel() < Logger::Info ) {
        log().setLogLevel( Logger::Info );
        log(Info) << argv[0] << " manually raises LogLevel to 'Info' (5). See also file 'orocos.log'."<<endlog();
    }

    log(Info) << "**** Creating the 'Hello' component ****" <<endlog();
    // Create the task:
    HelloWorld hello("Hello");
    // Create the activity which runs the task's engine:
    // 1: Priority
    // 0.01: Period (100Hz)
    // hello.engine(): is being executed.
    PeriodicActivity act(1, 0.01, hello.engine() );

    log(Info) << "**** Starting the 'Hello' component ****" <<endlog();
    // Start the component's activity:
    act.start();

    log(Info) << "**** Using the 'Hello' component    ****" <<endlog();

    // Do some 'client' calls :
    log(Info) << "**** Reading a Property:            ****" <<endlog();
    Property<std::string> p = hello.properties()->getProperty<std::string>("Property");
    assert( p.ready() );
    log(Info) << "     "<<p.getName() << " = " << p.value() <<endlog();

    log(Info) << "**** Sending a Command:             ****" <<endlog();
    Command<bool(std::string)> c = hello.commands()->getCommand<bool(std::string)>("Command");
    assert( c.ready() );
    log(Info) << "     Sending Command : " << c("World")<<endlog();

    log(Info) << "**** Calling a Method:              ****" <<endlog();
    Method<std::string(void)> m = hello.methods()->getMethod<std::string(void)>("Method");
    assert( m.ready() );
    log(Info) << "     Calling Method : " << m() << endlog();

    log(Info) << "**** Emitting an Event:             ****" <<endlog();
    Event<void(std::string)> e = hello.events()->getEvent<void(std::string)>("Event");
    assert( e.ready() );

    e("Hello World");

    log(Info) << "**** Starting the TaskBrowser       ****" <<endlog();
    // Switch to user-interactive mode.
    TaskBrowser browser( &hello );

    // Accept user commands from console.
    browser.loop();

    return 0;
}
