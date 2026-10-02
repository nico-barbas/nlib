# What is this?

An opinionated library replacing the need to use `libc` directly. Some of the internals still rely on `libc`, but the surface exposed is much nicer to use (in my opinion). It relies **heavily** on C23 and compiler extensions. It is meant to be used with `clang`. I have not tested compiling it with `gcc` and I am not planning to since most of my project are compiled with either clang (directly or through the zig toolchain).

Since it is so opinionated and specific, it is not meant to be dropped in every codebase and for every target platform. It covers all the platforms I usually target. However feel free to take the implementation you care about, take it out and modify it.

## disclamers

- `-fblocks` needs to be in the compiler flags, until your clang version supports `TS 25755`
- runtime.c **needs** to be compiled as its own translation unit. Without it, Debug builds fail to link
- `gfx` depends on SDL3

## core

A set of modules that handles most of what you'd expect from a standard library. It is heavily opinionated and tuned to how I like to write C.

### Most notable:

#### Types and runtime

Most of the basic types are covered. Some are aliases, some are defined as either \_BitInt or ..

`Type_Info` is there to make it easier for generic implementations to work (see `fmt` for an example).

`Option(T)` and `Result(T, E)` are probably where this library will either make it worth it for you or turn you off. Most of the up-to-date implementations in this library returns one of them. Each module has its own predefined error set. The logic behind it is that C cannot return multiple values making it extremely painful to return both a value and a potential error. This is my solution to provide better safety throughout this library and my other projects. Yes, it is annoying to check errors for every function call; yes, it results in a lot of `Result` and `Option` types being defined; but I believe it is worth it. It helped me catch many bugs that would've been silent otherwise. I could do the usually out paramters in every function, but I've grown accustomed to other modern languages which makes it annoying and unenjoyable for me.

To help alleviate that burden there is a set of macros handling most of the boilerplate:

- `some`, `none`, `ok`, `err`: Self explicit, they are just wrapper to initialize the structs without having to manually write it
- `try`: Used when the error set is shared. It propagates the error from a failed function call up to the caller and returns from the current function
- `or_return`: Used when the error set is not shared or that your function does not return a `Result(T, E)`. It returns the second argument to the caller.
- `unwrap`: Used when you want to assert that the result is ok

On top of that, there are a few niceties implemented to help with resource management:

- `defer`: Execute the block at function exit. It is LIFO, meaning that the last block will be executed first
- `errdefer`: Same as defer but only execute if the return value is an error. Since this is C, it is a bit more involved.

There are two flavours

One for functions that do not return a `Result(T, E)`

```c
My_Error my_function(Some_Data *data) {
  errdefer_scope;

  data->resource = make();
  errdefer {
    delete(data->resource);
  }

  commit();
  return My_Error_None;
}
```

One for functions that do return a `Result(T, E)`

```c
My_Result my_function(Some_Data_Create_Info *data, Allocator allocator) {
  errdefer_scope;

  Some_Data data = {0};
  data.resource = make();
  errdefer {
    delete(data->resource);
  }

  return_ok(My_Result, my_struct);
}
```

In either case, the function needs to know that everything is fine and to not run the `errdefer` blocks. This is done through either `commit()` or `return_ok()`

#### Allocator

All this library's functions that allocate memory go through an allocator interface. It currently provides a malloc wrapper and an arena implementation.

#### String

C strings are infamously annoying to use. The strings in this library are pointer + length (fat pointers) and runtime flags to handle dynamic allocation.

It also provides a lot of other things:

- Hand-rolled numerical string conversions
- A string builder: Provide your own storage and handles both direct type writing or through a fmt string. Currently there is two ways to write with it through a fmt string
  - `builder_write`: This uses the same format as C's `printf`. I would consider this implementation deprecated and would heavily recommend using the second one
  - `fmt_printb`: Format is `"{} some random text: {}", arg1, arg2`. This is the newer implementation that handles all the type safety through `_Generic`. One advantage of doing this way is that it provides compile time errors for unsupported types. You can define your own types with it by extending the `FMT` macro in your own code without touching this implementation:
  ```c
  #undef FMT_EXT
  #define FMT_EXT(value) _Generic((value), Vec2: vec2_fmt, default: unsupported_fmt)
  ```
  The only downside is that you can only have one per project.

#### List

Generic type-safe (truly) dynamic array implementation exposed through a monomorphic header.

How to include for a specific type:

```c
#define LIST_TYPE String
#define LIST_TYPE_NAME String_List
#define LIST_FUNCTION_PREFIX string_list
#include "list.h"

String_List_Make_Result result = make_string_list(my_cap, my_allocator);
// It is good practice to check the ok and error fields of results. See types and runtime for more info
String_List my_string_list = result.value;
unwrap(string_list_push(&my_string_list, my_string));
delete_string_list(...);
```

This can be done any number of times and it handles all the undefs by itself.

#### Map

Open address hashmap implementation. It is not type-safe and I wouldn't recommend using it as is. It uses a lot of `void *` and I am meaning to replace it with the same kind of monomorphic header as the list implementation

#### Math

Most of what you'd expect from a math library. It covers both what C's `math.h` already covers but with four notable additions:

- Bit manipulation: Sign extension, getting the raw bits out of floating numbers
- Hashing: Currently only `fnv1a` is implemented. This is what the rest of the library relies on (map, rand)
- Safe math: Safe math operations that check for integer overflow and underflow. Currently only the ones I needed for various projects are implemented
- Linear algebra: Not much to say. For the types that make sense, the implemention uses simd (except Mat4, I never got around to it). Be careful with the matrix helpers, they are meant for right handed coordinate systems

#### Rand

Not much to say. C's rand is not the best for random generation. This is mostly stubbed but it provides a generic `Random_Generator` interface with the same mindset as the allocator. Consumers can then take it and not care about how the random number is generated.

It also provides one implementation of `PCG32` random number generator.

#### IO

Very early with only one function I needed: `read_entire_file` that uses libc.

## gfx

This is my personal graphics framework for the projects I am developping. Not meant for others to use yet. It relies on `core` and is very low-level.

- gfx abstraction: wraps most of SDL3 GPU I care about. Provides input handling (keyboard, mouse, tablet). There are some design issues since some of the gpu operations are tied to the `App` global variable. Again not meant for others to use
- imgui: Immediate mode UI implementation. Very convenient can be reused anywhere but you need a drain for the render commands it generates. It is platform and graphics api agnostic, meaning it could be used in any project. I used it in the past with `Raylib` and some other framework I developped (WebGPU).
- camera: Some camera implementations. Project agnostic, you can use them or not, the rest of the lib doesn't care. It still relies on the platform layer for (although this will be patched).
  - orbit camera: third person games, strategy/sim games
  - first-person camera: self explicit
  - chase camera: for cinematics or car games
- physics: Very early and uncomplete. It handles some basic collision needs I had in my previous projects
