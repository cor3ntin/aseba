TDM architecture
================

Event Loop and Threading
------------------------

The TDM, by large, uses a single thread,
A few services are offloaded to secondary event loops, notably the
monitoring of TCP aseba nodes zeroconf events, as well as firmware update tasks

Most actions are triggered by incoming connections, incoming messages,
or in a few cases by timer events.

The event loop is started in ``main.cpp`` after each server
(where a server is an object listening for incoming connections, TCP, WebSocket, serial).

This means that once a few services are initialized, the event loop
runs forever - the only way to stop the server is to kill it with Ctrl+Cl.
or a ``DeviceManagerShutdownRequest`` message is received by a local
endpoint.

Most I/O operations in the TDM are non-blocking.
It is important to take care not to run long operations in the events loop,
because that delays the processing of incoming events.

In practice, this solution scale to hundreds or more or aseba nodes.


Ownership and lifetime
----------------------

Both client endpoints and Aseba endpoints follow the same basic architecture.
These objects are allocated on the heap and run an asynchronous read loop.
The code below describes the architecture of both the aseba endpoint
and the client endpoints.

.. code-block:: c++

    class endpoint : public std::enable_shared_from_this<endpoint> {
        public:
            // When a client connects, it will be allocated
            // in a shared_pointer by the server.
            // A start method is then called which starts a read-loop
            void start() {
                read_message_async(shared_from_this());
            }
        private:
            void read_message_async(std::shared_ptr<endpoint> self) {
                // The callback captures a copy of the shared pointer.
                // Which means the reference count of this instance will be at least
                // 1 until the callback is called.
                // ensuring we won't be destroyed in the meantime.
                // The lifetime of endpoints objects is only assured by
                // that shared pointer passing.
                auto callback = [self](auto error, auto received_message) {
                    if(error) {
                        // When a an error occurs while reading from the network,
                        // We assume the remote endpoint has disconnected or is otherwise
                        // unresponsive, and just stop trying to read further incomming message.
                        // This will decrement the reference counter.
                        // When the reference counter reaches 0, the endpoint is destroyed.
                        // The destructor is run at that point,
                        // Some cleanup code may be included in that destructor.
                        return;
                    }
                    // If there is no error, we enqueue another read
                    // Here again, we pass the instance as a shared pointer
                    // to extend its lifetime.

                    // We enqueue the next read first , not to forget it.
                    // This calls won't do work until the even loop is re-entered,
                    // when the callback exits.
                    read_message_async(self);

                    // We probably want to do something with the received message.
                    process_received_message(received_message);


                };
                // Enqueue an async read operation in the Asio event loop.
                // When the operation completes, the callback will be called.
                read_async(callback);
            }
    };

In the general case, objects that represent a network connection are only owned by themselves.
It is important not to hold onto ``shared_ptr`` to these objects; otherwise, they would
remain in memory even when disconnected.
When you need to keep track of them, use a ``weak_ptr``, which can be converted to a ``shared_ptr``
on-demand without actually incrementing the reference count. `[Smart pointers tutorial] <https://embeddedartistry.com/blog/2017/01/04/c-smart-pointers/>`_.

Write loops
------------

Both clients and aseba endpoints are duplex connections.
And because the TDM is event-driven, each message received, timer even, etc can start
arbitrary chains of asynchronous computations.

To ensure writes are not interleaved and sent in the right order,
each endpoint maintains a queue of messages to send


.. code-block:: cpp

    class endpoint : public std::enable_shared_from_this<endpoint> {
        public:
            void write_message(Message message) {
                // Messages are always enqueued in the instance-owned message queue.
                // This will ensure they remain alive for the duration of the
                // write operation.
                queue.push(message);

                // If the queue is not empty, an async write loop already exist (and was triggered by another write)
                // We let the previously started writing loop empty the queue
                if(queue.size() != !)
                    return;
                // Write one message
                write_one();
            }
        private:
            void write_one() {
                // The callback owns a reference to that instance so it remains
                // alive for the duration of the write operation
                auto callback = [self = shared_from_this()](auto error) {
                    // Once a message has been written it can be discared
                    queue.pop();

                    // Continue emptying the write queue until everything has been sent
                    if(!queue.empty())
                        write_one();
                }
                // Write the message at the top of the queue
                async_write(queue.front(), callback);
            }
    };

Strands
-------

Even if the TDM is a single-threaded application, an individual endpoint
could find itself in a situation where it tries to do multiple read or writes concurrently.
To avoid that, most operations are protected by a `strand <https://www.boost.org/doc/libs/1_76_0/doc/html/boost_asio/overview/core/strands.html>`_.
which guarantees a sequential order of operations.

.. code-block:: cpp

    auto callback = [] { printf("Done"); };
    auto protected_callback = boost::asio::bind_executor(strand, callback);
    do_something_async(param, protected_callback);

Signals
-------

`Boost.Signals2 <https://www.boost.org/doc/libs/1_61_0/doc/html/signals2.html>`_ is used to broadcast information between entities
while avoiding tight coupling.



.. code-block:: cpp

    struct receiver;
    struct emitter {
        private:

        // An alias for our signal, taking 2 parameters.
        // We need to pass ourselves as parameter so that receivers know where the signal comes from!
        using my_signal_t = boost::signals2::signal<void(emitter*, int)>;,
        my_signal_t my_signal;

        public:

        // We expose a method so that listeners can connect to the signal without
        // having to make the signal public
        template <typename callback>
        boost::signals2::scoped_connection connect(callback&& cb) {
            return my_signal.connect(std::forward<callback>(cb);
        }

        private:

        void notify() {
            // the signal is invoked like a function with is arguments
            my_signal(this, 42);
        }

    };

    struct receiver {
        boost::signals2::scoped_connection connection;

        // When a signal is received, dispatch to a non static member function
        static void on_signal_received(receiver* self, emitter* e, int param) {
            self->on_signal_received(e, param1);
        }

        void on_signal_received_private(emitter*, int param) {
            // something exciting happen
        }

        void connect() {
            // connect to the signal
            connection = emitter->connect(
                // Create an invkable that binds the first argument
                // to an instance of the class so that we can refer to the receiver when the
                // signal is emmitted
                std::bind(&application_endpoint::on_signal_received, this,
                    std::placeholders::_1, //  first signal parameter (receiver*)
                    std::placeholders::_2) //  second signal parameter (int)
            );
        }
    };

List of signals
****************

* ``aseba_node`` will emit a signal for each Aseba Event (``events_watch_signal_t``)
* ``aseba_node`` will emit a signal for each change of variable in the VM (``events_watch_signal_t``)
    Variables are only monitored when the signal is connected to at least one receiver due to the relatively high bandwidth requirements for wireless nodes.
* ``aseba_node`` will emit a signal when the state of the VM changes (execution start/stop/breakpoint/etc.)
* ``group`` will emit a signal when the table of Aseba events is changed.
* ``group`` will emit a signal when the list of Aseba shared variables is changed.
* ``group`` will emit a signal when a scratchpad changes
* ``aseba_node_registery`` will emit a signal when a node is connected/disconnected/ready/locked