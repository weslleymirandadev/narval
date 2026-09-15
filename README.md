# Narval

A high-performance, multiparadigm compiled programming language with inferred typing that uses implicit and inferred Ownership & Borrowing concepts (YES, without explicit annotations, without borrow errors in the user's face). The compiler assumes responsibility for memory, parallelism, and safety — without imposing a new mental model on the programmer.

Narval transfers the operational responsibility of the code to the compiler, while preserving predictability, performance, and control when needed. If it can be proven safe, Narval does it automatically. Unlike Rust, Narval does not try to teach the programmer how to write correct code; it tries to make ordinary code behave like expert code.
