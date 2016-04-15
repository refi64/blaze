Blaze
=====

Welcome to the source code for Blaze!

What you're seeing here is horribly incomplete. If you don't mind toying with
something that doesn't really work ATM, continue reading.

What is Blaze?
**************

Blaze is my idea for YALLPL (Yet Another Low-Level Programming Language). Even
though there are already a LOT of these, Blaze has some unique ideas that I hope
can help it stand out:

- `Smarter error handling`_.
- An elegant, Python-inspired syntax.
- A Go-inspired module system.
- Assume symbols are public by default (private ones should be prefixed with
  ``_``).
- Fast compile times.
- Support for procedural/data-style structs, OO-style classes, and FP-style
  unions/ADTs.
- Pass everything by reference by default.
- Generics.

Current status
**************

99% of this hasn't been implemented yet, despite the fact that the compiler clocks
in at a whopping >3k LOC. However, you can write really basic programs, and some
more complex control flow structures (like if statements) work.

Structs are present, as is overloading. But a lot of other stuff doesn't work:

- Properly calling destructors (see "Destructors" section in proto.md).
- Modules.
- Classes.
- Unions.
- Generics.
- Consistency. (I have no clue why I'm writing this in reStructuredText but wrote
  proto.md in Markdown...)
- Sanity.

Use it at your own risk!

.. _Smarter error handling:
    http://kirbyfan64.github.io/posts/an-idea-for-concise-checked-error-handling-in-imperative-languages.html
