Libraries
==========

Asio
----

The Asio library is used by the Thymio Device Manager for both networking and serial communication and as a basis of its asynchronous model. A good understanding of the Asio architecture
is desirable to maintain the Thymio Device Manager.

The documentation for this project can be found at `on its website <http://think-async.com/Asio/Documentation.html>`_.
We used the version of the library that is part of the boost project.

We would also recommend watching the talk `Asynchronous IO with Boost.Asio <https://www.youtube.com/watch?v=rwOv_tw2eA4>`_ from Michael Caisse, which was used as an inspiration for the architecture of the TDM. This talk goes over the asynchronous model used in Asio.

.. raw:: HTML

    <iframe width="560" height="315" src="https://www.youtube.com/embed/rwOv_tw2eA4" title="YouTube video player" frameborder="0" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture" allowfullscreen></iframe>


Spdlog & fmt
-------------

Spdlog is a logging framework - and `fmt` is the string formatting library it uses.

Documentation for ``spdlog`` can be found on `Github <https://github.com/gabime/spdlog/wiki/1.-QuickStart>`_.

Documentation for ``fmt`` can be found `here <https://fmt.dev/latest/index.html>`_.

``fmt`` uses a syntax similar to that of Python string formatting.


Zeroconf
--------

Zeroconf is a DNS-based service discovery on the local network, which allows
The TDM to

* Automatically discover simulated devices - and Thymio 3 in the future -
* Be discovered automatically by clients.

Note that, as with any network service, it is still possible to connect to a Thymio Device Manager
directly by its IP/port.

``Avahi``, ``mDNS`` and ``Apple Bonjour`` are slightly different implementations of the same technology.
To abstract these differences, we use
the open-source ``aware`` library, which adds zeroconf capabilities to ``Asio``.

Because the ``aware`` project is no longer maintained, it is now maintained by Mobsya.
The library is on the `Mobsya's Github <https://github.com/Mobsya/aware>`_.


Other Libraries
---------------

The TDM uses the following supplementary libraries.

* ``boost::program_options``: A boost library for command-line argument handling.
  [ `Documentation <https://www.boost.org/doc/libs/1_76_0/doc/html/program_options.html>`_ ]

* ``pugixml``: A library for XML parsing. Used for AESL files which are XML files
  [ `Documentation <https://pugixml.org/>`_ ]

* ``ranges-v3``: This library abstracts a pair of iterators into a range and provides useful algorithms.
  [ `Documentation <https://github.com/ericniebler/range-v3>`_ ]

.. note::  ``<ranges>`` is available in C++20 and provides the same features as ``ranges-v3``.
   A migration should be envisaged, pending compiler support.
   [ `Ranges <https://en.cppreference.com/w/cpp/ranges>`_, `span <https://en.cppreference.com/w/cpp/container/span>`_, `Algorithms <https://en.cppreference.com/w/cpp/algorithm>`_ ]


* ``mpark::variant`` : An interface compatible replacement for ``std::variant`` which is needed because the libc++ (clang) implementation
  did not support defining recursive variant.

.. note:: It might be worthwhile to investigate removing that dependency in the future. When this was implemented in 2019,
   both MacOS and android suffered from that ``std::variant`` issue.


* ``tl::expected``: ``expected<Result,Error>`` is either a Result or an Error. It is similar to the ``Either`` type in Haskell or C#. A similar type might be added to C++23.
  [ `Documentation <https://tl.tartanllama.xyz/en/latest/api/expected.html>`_ ]


Flatbuffers
------------

Flatbuffers is a serialization library used between the TDM and its clients.
It was chosen because libraries exist for it in many languages, including C, C++, Python, JS, and Swift.
Its high-performance zero-copy design makes it usable by the Thymio 3 in the future.
It is actively maintained and draws from the lessons learned on Protocol Buffers to offer a better design.
Notably, it generates cleaner C++ code.

* `Introduction (video) <https://www.youtube.com/watch?v=90ND0yQVYg8>`_
* `Reference <https://google.github.io/flatbuffers/>`_
* `Github <https://github.com/google/flatbuffers>`_


C++
----

The Thymio Device Manager is implemented in C++17 following best practices.

* `Language & Library reference <https://en.cppreference.com/w/>`_
* `C++ Standard (HTML) <http://eel.is/c++draft/>`_
* `Core guidelines <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines>`_ : An extensive set of (mostly) great advice maintained by C++ experts!

Recommended books
-----------------

* A Tour of C++, Bjarne Stroustrup (introduction material)
* Effective Modern C ++, Scott Meyers (best practices)


CMake
-----

Cmake is used as the Build system.

* `Official documentation <https://cmake.org/documentation/>`_
* `Recommended book <https://crascit.com/professional-cmake/>`_
* `Tutorial (Blog) <https://www.siliceum.com/en/blog/post/cmake_01_cmake-basics/>`_