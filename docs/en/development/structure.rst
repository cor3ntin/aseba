Files organization
==================

This document describes the structure of the Aseba project and TDM source files.

``aseba`` directory
-------------------

.. code-block::

    .
    ├── clients
    ├── CMakeLists.txt
    ├── common
    ├── compiler
    ├── flatbuffers
    ├── launcher
    ├── qt-thymio-dm-client-lib
    ├── targets
    ├── thymio-device-manager
    ├── transport
    └── vm


.. list-table:: Aseba directory
   :header-rows: 1
   :widths: 25 1000

   * - Node
     - Description
   * - ``clients``
     - Sources of Aseba Studio, VPL, and legacy utilities superseded by the ``launcher``.
       Aseba Studio is a client from the point of view of the TDM, and other tools connect directly to Thymio 2 via a wireless or serial connection.
   * - ``common``
     - Files and assets shared by the ``thymio-device manager``, the launcher, the clients, and in some cases, the Thymio 2 firmware.
   * - ``compiler``
     - This is the Aseba compiler, converting an Aseba program into bytecode + metadata (like the list of Aseba variables).
       The compiler is compiled as a static library and linked into the ``thymio-device manager``. There exists no standalone compiler.
   * - ``Flatbuffers``
     - Contains thymio.fbs that describes the Flatbuffers message used to
       communicate between the ``thymio-device-manager``and the machinery to generate the C++ serialization code using the Flatbufers compiler (``flatc``).
       The ``thymio.fbs`` is a standalone file that can be distributed and used by providers of ``thymio-device manager`` clients.
       However, C++ projects within the Aseba repository use the generated ``thymio-flatbuffers`` library.
   * - ``launcher``
     - Sources & assets for the Thymio Launcher
   * - ``qt-thymio-dm-client-lib``
     - Sources of a library shared by the ``launcher``, ``VPL``, ``Aseba Studio`` and other ``Qt`` based TDM clients.
       This library handles networking, serialization, and a few types for these clients.
       Notably, it provides a Qt-based wrapper for flatbuffers and QML-friendly Types to encapsulate the different entities used by these clients (``Scratchpad``,
       ``ThymioGroup``, and ``ThymioNode``)
   * - ``transport``
     - Transport layer for Thymio-2 like robots (Playground, Thymio2, E-puck, etc.).
       The source files are used as part of the Thymio 2 firmware build process and need to remain in the subset of C understood by the microchip compiler.
   * - ``vm``
     - Source files for the Aseba VM.
       The source files are used as part of the Thymio 2 firmware build process and need to remain in the subset of C understood by the microchip compiler.
   * - ``thymio-device-manager``
     - The sources for the TDM, described in the next section


TDM Organization
================
Most of the sources for the TDM are in headers as the project makes use of templated code.


``main.cpp``
------------

Program entry point.
Handles starting the servers and registers them on mDNS.
There are 3 "Servers" in the Asio sense, aka classes that monitor and accept incoming connections.

*  TCP Server for TDM clients
*  WebSocket Server for TDM clients - this is a separate server because the port is different, but the code is mostly the same
*  USB server for Aseba Nodes
*  TCP for aseba nodes

In addition, before starting the servers, we:

* Add a signal handler to handle Ctrl+.C and gracefully kill the server,
* Setup a lock file to ensure no two TDM instances run concurrently on the same machine.
  This avoids confusions and issues with zeroconf which cannot deal gracefully with multiple identical services per host
* Register a number of "Asio Services", which are global utilities whose lifetime is bound to that of the Asio event loop.


Services
--------

* ``system_sleep_manager`` in ``system_sleep_manager.h`` : This service prevents the system (Windows 10/OSX) from going to sleep while there are connected clients, which is useful in a classroom situation!

* ``firmware_update_service`` in ``fw_update_service.[h|cpp]``: This will monitor node connection events (see ``node_status_monitor``),
  and check for possible Thymion 2 firmware updates on the Mobsya website.

  The TDM caches the result indefinitely and needs to be restarted to clear that cache.
  Firmware updates will be checked for all devices but can only be applied to a robot connected by USB directly.

* ``wireless_configurator_service`` in ``wireless_configurator_service.[h|cpp]``: Handle the pairing between a Thymio 2 and a wireless dongle.

* ``aseba_node_registery`` in ``aseba_node_registery.[h|.cpp]`` : Maintains a list of connected aseba nodes
   * Broadcast information about each node on zeroconf
   * Broadcast node status to connected applications
   * Maintains a node <-> group mapping so that nodes are reconnected to the same group
   * Broadcast the node status changes to other services and application endpoints

* ``uuid_generator`` in ``uuid_provider.h``: generates 128 bits UUID - The service is merely used as a way to store the seed.
* ``aseba_nodeid_generator`` in ``aseba_nodeid_generator.h``: similarly, this generates a 16 bits aseba node id.



Applications
-------------

In the TDM source code, the terms App/Application refers to a TDM client (by opposition to an Aseba node).
Applications include Aseba Studio, VPL, the launcher, etc.
They connect via Websocket or TCP.

* ``application_server`` in ``app_server.[h|cpp]`` : listen to incoming connections on the TCP or Websocket port and construct an ``application_endpoint`` each time there is an incoming connection. This runs in a loop until the application is stopped.

* ``application_endpoint`` in ``app_endpoint.[h|cpp]``: This handles the connection with a single application (so there is one instance of this class per connected application).

  Its responsabilities include:

  * Monitoring and treating incoming messages
  * Handling node locking
  * Serializing and sending information about nodes to the connected client. All the flatbuffers transport logic is handled there
  * Other network-related responsibilities such as pinging the client, protocol handshake, etc

  Instances of this class are always wrapped in a ``shared_pointer, `` and they own themselves:
  They self-destruct once the client is disconnected (no pending read and write operations). It is therefore important to always make sure a read will be performed when the event loop is reentered, which can be done by calling ``application_endpoint::read_message``
  Because they handle all the client communication, most TDM evolutions will involve adding message handling in ``handle_read``


* ``flatbuffers_message_writer.h``, ``flatbuffers_message_reader.h``:
   These classes are `Asio Composed Operation <https://www.boost.org/doc/libs/1_76_0/libs/beast/doc/html/beast/using_io/writing_composed_operations.htm>`_
   respectively writing and reading exactly one Flatbuffers message on the socket, asynchronously.

.. code-block:: cpp

  | size : uint32_t (big endian) | Flatbuffer payload

``flatbuffers_messages.h``: a collection of free functions to serialize specific objects in order to keep the size of ``application_endpoint`` manageable.


Aseba Nodes
------------

Aseba Nodes can form a network of their own, for example, a dongle is considered an aseba node even
if it only serves to relay messages to robots,
Inversely, not all nodes have a physical connection to the TDM. A Thymio 2 wirelessly connected does not connect
directly to the TDM.
As such, the TDM separates the physical connection (``aseba_endpoint``) from virtual nodes (``aseba_node``).

* ``aseba_device`` in ``aseba_device.h``:
  The TDM supports:

  * Dongles over USB
  * Thymio 2 over USB
  * Simulated Robots over TCP

  ``aseba_device`` is a variant of these transport layers and handles the lowest level (disconnection, USB device ID, etc.).

* ``aseba_endpoint`` in ``aseba_endpoint.[h|cpp]``: Handles communication between a device (dongle, USB robot or playground).
  This includes aseba message serialization, reading, writing, and initializing firmware updates.

  In addition, this class is in charge of sending ping messages to all nodes connected to a dongle endpoint,
  and to discover and maintain a list of nodes connected to the dongle.
  New ``aseba_node`` instances are created when a new node is discovered.

  Lastly, because events and shared variables are shared by all nodes in an aseba network,
  ``aseba_endpoint`` is responsible for broadcasting these events and keeping the shared variables in sync.
  This means that all nodes connected to the same ``aseba_endpoint`` are in the same ``group``


* ``aseba_message_parser`` and ``aseba_message_writer`` (in ``aseba_message_parser.h`` and ``aseba_message_writer.h``)
  These classes are `Asio Composed Operation <https://www.boost.org/doc/libs/1_76_0/libs/beast/doc/html/beast/using_io/writing_composed_operations.htm>`_
  that read or write a single ``Aseba::Message`` asynchronously on a device.

* ``aseba_node`` in ``aseba_node,[h|cpp]`` handles an individual node. Sending and Writing messages is handled
  by the endpoint. an aseba node is identified by a 16 bits number in the aseba protocol and by a UUID in the TDM.
  That UUID is generated the first time a Thymio 2 connects to a TDM and then persisted in the Thymio 2 ROM.
  ``aseba_node`` handles all node-specific messages and operations. This includes

  *  Compilation
  *  Sending bytecode
  *  Synchronizing variables with the VM
  *  Controling the VM execution (including debug)
  *  Handling received events




Playground connections
-----------------------

* ``aseba_tcp_acceptor`` in ``aseba_tcp_acceptor.[h|cpp]``
  This class handle discovery (using zeroconf) and connection to Aseba playground (using TCP).
  The TDM is a client of each playground rather than a server.
  The class works by monitoring zeroconf events and connects to each service whose name is ``aseba``.
  This means each playground advertises itself as being an ``aseba`` on a different port.
  For security reasons, connections to playgrounds are only possible on the locale network.
  The TDM cannot support playgrounds without zeroconf support.
  The TDM will use zeroconf record properties (``protovers`` and ``type``) to determine the type of simulated robot and the version of the simulated robot's protocol.
  The protocol used to connect with the playground once connected is exactly the same Aseba Protocol used over USB.

  In the future, this class could accept Thymio 3 connections over wifi, using the same mechanism, but potentially
  creating an endpoint that uses a flatbuffer-based protocol.

USB connections
---------------

Providing stable USB connections for Thymio 2 proved to be a major challenge in the project.
There be dragons
There are two sets of classes dedicated to USB transfers.

Serial USB
***********

The serial classes use `asio::serial_port <https://www.boost.org/doc/libs/1_76_0/doc/html/boost_asio/reference/serial_port.html>`_
to connect to USB devices,
Thi plugs to the Operating System serial/COM devices support and does not require specific root/admin access.

These classes are used by default on all platforms.

Serial connections mirror the architecture of other networking components in the TDM

* ``boost::asio::serial_port`` represent a single physical connection to an usb device.
  This will always be encapsulated in an ``aseba_endpoint``

* ``serial_acceptor`` in ``serialacceptor.h`` and the platform-specific implementation files,
  monitor for devices being physically plugged in a USB port.
  This is implemented in a platform-specific manner for Windows, OSX, and Linux

  * Windows: We are using ``SetupDiGetClassDevs`` periodically on a timer.
    So it's an active loop that rescans all the connected devices every few seconds.
    Some incantations are used to extract the name, vendor, and product id of each device.
  * OSX: We are using a similar design, but using the `OSX IOKit Framework <https://developer.apple.com/documentation/iokit/iokitlib_h?language=objc>`_
  * Linux: We use the `udev library <https://www.freedesktop.org/software/systemd/man/libudev.html>`_.
    The behavior there is a bit different. Instead of having a timer-triggered loop,
    we get passively notified each time an event (connection, disconnection) occurs and then relist the devices.

In all cases, we maintain a list of currently connected devices.
This list is then immediately consumed by ``serial_server``

* ``serial_server`` in ``serialserver.[h|cpp]``: This construct in a loop ``aseba_endpoints`` from serial devices found by ``serial_acceptor``.

LibUSB classes
**************

The files

* ``usbacceptor.[h|cpp]``
* ``usbcontext.h``
* ``usbdevice.[h|cpp]``
* ``usbserver,[h|cpp]``


Provides an implementation of USB devices using `libusb <https://libusb.info/>`_
This was part of an effort to improve connection stability with wireless thymios
and to support Linux platforms where ``libudev`` does not exist, notably Android``.
However, that effort had mixed results, and the Serial implementation
proved more robust.the compile-time defines ``MOBSYA_TDM_ENABLE_USB`` and ``MOBSYA_TDM_ENABLE_SERIAL``
define which USB implementation is used, and are set by ``cmake`` in ``CMakeLists.txt``.
These USB implementations are mutually exclusive.


Logic entities
--------------

Properties
***********

A property is a structure very similar to a JSON Object.
It is a variant of  ``null | boolean | integer | double | string | list<property> | map<string, property>``.

It is used by the TDM to exchange variables between itself, Aseba nodes, and clients.
It is important to note that this structure is forward-looking.
In Aseba, everything a list of 16 bits number.
However, we wanted future robots to expose a richer type system. for example, robots based on micro-python, etc.,

* ``property.h`` implements the C++ type that models a property stored in a variant (a fancy discriminated union).
  The type offers conversion methods to from the types it can contain.
  the ``std::ostream& operator<<`` at the end of the file is a good example of how such type can be traversed.
  Note that because it is a recursive data structure, it can have an arbitrary depth.
* ``aseba_property.h``: Functions to convert a property to/from an aseba variable. Note that an aseba variable is nothing more than a ``vector<int16_t>`` so these conversions may return an error!

* ``property_flatbuffers.[h|cpp]``: Functions to convert a property to a `Flexbuffer <https://google.github.io/flatbuffers/flexbuffers.html>`_.
   which uses the same serialization mechanism as flatbuffers but are not validated against a schema.
   Flexbuffers can represent any property.

   Similar [de]serialization has been written in the ``qt-thymio-dm-client-lib`` Qt lib for use with the Qt clients.
   Flexbuffers APIS exists for ``Typescript``, ``Python``, ``Go``, and ``Rust``.
   A Swift implementation exists `outside of the flatbuffers project <https://github.com/mzaks/FlexBuffersSwift>`_

   Because the Typescript implementation did not exist at the time, VPL 1 uses a wasm-module compiled from the C++
   version. The sources of this project are `on Github <https://github.com/cor3ntin/flexbuffers-js>`_



Groups
******

Groups, implemented in ``group.[h|cpp]`` represent a group of Aseba nodes.
Each aseba node belongs to exactly one group, while multiple nodes can belong to exactly one group.
When a node first connects, it is assigned a group. There is no node that does not belong to a group.

A group represents an Aseba network.
This means all the nodes that are associated with the same endpoint belong to the same group.
But multiple endpoints can be in the same group.

Groups serve multiple purposes, centered around a simple idea: the lifetime of an aseba node ends when it is physically
disconnected - like all other network objects, they self-destruct when the connection is closed.

A group, therefore, saves the state of nodes and endpoints so that no data is lost by spurious or short disconnection.
All the permanence (in relative terms, nothing is ever saved on disk) is handled by groups.

Permanent state includes data shared among all nodes of the same group.

* Events
* Shared variables

As such, events and shared variables are created on a group and then broadcasted to all endpoints in that group.
When events are triggered, they go from a node, to a group, to all other endpoints in that group (At the same time, they are broadcasted to all clients).

Because AESL files describe a network of nodes, opening them is handled by the ``group`` class.
Loading an AESL file will reset the sets of events and shared variables for all nodes/endpoints in the group.
It will also set ``scratchpad``.

Scratchpads
***********

A scratchpad is a text buffer that contains the text code that will be run on each node.
It is essential to realize that the TDM manages 100% of the state of the robots.
Each time a key is pressed in Aseba Studio, the scratchpad of the corresponding node is modified in the TDM.
When an AESL file is opened, the entire content of the file is sent to the TDM, which
parse it, assign code to each node, deal with events, variables, and the result
of that is sent to every client.
This allows a teacher to monitor students as everything is centralized.
It also allows us to centralize the compilation and syntax checking architecture.
Any parsing error is sent back to the client.


When a node connects, ``aseba_node_registery`` and ``group`` have logic to assign a node to a group and a node to a ``scratchpad``.
The goal was to make disconnections (which are frequent over wireless) as disruptive as possible.