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

Structs are present, as is overloading, but you can't overload constructors, nor
can you overload magic methods (such as ``__copy__``). The eventual goal is to
phase out the magic methods in favor of something more elegant (such as ``dup``),
but they're kind of stuck here ATM.

.. _Smarter error handling:
    http://kirbyfan64.github.io/posts/an-idea-for-concise-checked-error-handling-in-imperative-languages.html
