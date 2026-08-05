#pragma once

#include <Python.h>
#include <stdbool.h>

/*
 * What a struct class was asked to be. Every option is inherited from the
 * struct base and overridden by a class keyword, so a subclass behaves like its
 * base until its own body says otherwise.
 *
 * Only `frozen` refuses to differ from the base, and only mutation earns that:
 * a mutable subclass of a frozen one hands a writable object to everything
 * holding a reference of the base's type. Nothing else here can break a promise
 * the base made about a value already in flight.
 */
struct options {
	bool frozen;
	bool eq;
	bool order;
	bool repr;
	bool match_args;
	bool weakref;
};

/* Field by field, because a struct of bools has padding and memcmp would read
 * it. Adding an option to the struct without adding it here is the one way to
 * get this wrong, which is why they are spelled rather than counted. */
static inline bool options_agree(struct options const left, struct options const right) {
	return (
		left.frozen == right.frozen &&
		left.eq == right.eq &&
		left.order == right.order &&
		left.repr == right.repr &&
		left.match_args == right.match_args &&
		left.weakref == right.weakref
	);
}

/* Whether the class body's keywords were accepted, and what they resolved to. */
struct options_request {
	enum { OPTIONS_RESOLVED, OPTIONS_REJECTED } tag;
	struct options options;
};

/* Where a class with no struct base starts. */
static inline struct options options_initial(void) {
	return (struct options){
		.frozen = true,
		.eq = true,
		.order = false,
		.repr = true,
		.match_args = true,
		.weakref = false,
	};
}

/*
 * `base_is_constraining` is false for a base with no fields, which has nothing
 * to mutate and so does not pin `frozen` -- that is what lets a first subclass
 * of Struct ask to be mutable at all.
 */
struct options_request options_read(
	PyObject * keywords,
	struct options inherited,
	bool base_is_constraining
);
