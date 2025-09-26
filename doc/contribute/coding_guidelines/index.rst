.. _coding_guidelines:

=================
Coding Guidelines
=================

.. contents::
   :local:
   :depth: 2

Coding Style
============

Zephyr's coding style is described in the `Zephyr Coding Style Guidelines <https://docs.zephyrproject.org/latest/contribute/style/index.html>`_. We want to follow this style for new files being introduced.

If in doubt, you can always reformat a file by running ``clang-format -i <file>`` - but please create a distinct commit when doing this, and don't mix formatting changes with functional ones!

Zephyr Coding Guidelines
========================

Zephyr's coding guidelines are described in the `Zephyr Coding Guidelines <https://docs.zephyrproject.org/latest/contribute/coding_guidelines/index.html>`_.

It is advised to follow the guidelines to the best of your ability, however, some rules are quite difficult to achieve compliance with.

Additional Guidelines
=====================

The following additional guidelines apply to this project:

struct and enum Definitions
---------------------------

``struct`` and ``enum`` definitions shall be preferred in place of ``struct`` typedefs.

**Example - Non-compliant:**

.. code-block:: c

   typedef struct {
       uint32_t field;
   } foo_t; /* non-compliant */

**Example - Compliant:**

.. code-block:: c

   struct foo {
       uint32_t field;
   }; /* compliant */

**Example - Non-compliant:**

.. code-block:: c

   typedef enum {
       ENTRY
   } my_enum_e; /* non-compliant */

**Example - Compliant:**

.. code-block:: c

   enum my_enum {
       ENTRY
   }; /* compliant */

**Exception: Function Pointers**

Function pointer typedefs are acceptable and preferred for readability and maintainability.

**Example - Acceptable:**

.. code-block:: c

   typedef int (*message_handler_t)(const union request *req, struct response *rsp);
   typedef void (*callback_func_t)(void *data);

   struct message_dispatcher {
       message_handler_t handler;
       callback_func_t callback;
   };

Naming Conventions
------------------

Snake case shall be preferred over camel case for variable names, function names, and other identifiers.

**Example - Non-compliant:**

.. code-block:: c

   int myVariableName;          /* non-compliant */
   int calculateMaxValue(void); /* non-compliant */
   struct myDataStruct {        /* non-compliant */
       int fieldValue;          /* non-compliant */
   };

**Example - Compliant:**

.. code-block:: c

   int my_variable_name;          /* compliant */
   int calculate_max_value(void); /* compliant */
   struct my_data_struct {        /* compliant */
       int field_value;           /* compliant */
   };
