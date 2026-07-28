#pragma once

/*
 * In-file unit tests, Rust style: each translation unit keeps its tests at the
 * bottom, behind TESTING, where they can see its statics. The whole file is
 * inert without that flag, so a normal build never sees any of it.
 *
 * The subject is C that speaks to the CPython API, so the runner embeds an
 * interpreter rather than faking one -- there is nothing to mock, and a fake
 * PyObject would be testing the fake.
 */
#ifdef TESTING

#	include <Python.h>
#	include <unity.h>

/* Run `source` as a module body with `jpstruct` already imported, and return
 * the value bound to `result`. Aborts the test on any Python error. */
PyObject * testing_evaluate(char const * source);

/* Each translation unit's suite, run by tests/c/main.c. */
void construct_tests(void);
void fields_tests(void);
void meta_tests(void);
void options_tests(void);
void owned_tests(void);
void repr_tests(void);

#endif
