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

Per commit, you should run ``clang-format -i <file>`` and commit the formatting changes with functional changes. This rule will be enforced on a case-by-case basis.
For example, a functional change which introduces  many lines of unrelated tabbing/spacing changes could be split based on developer discretion.
Regardless of if formatting is applied per functional commit, or in one final formatting commit, the overall changes of any PR to main must pass ``clang-format`` checks.

Every commit should adhere to ``checkpatch`` guidelines, along with the other zephyr compliance checks. Errors must be addressed. Warnings should be considered.

All commit compliance, including ``checkpatch`` and ``clang-format``, are captured by running ``./scripts/check_sysfw_compliance.py -c origin/main..``, which you can run locally on your branch.
Note this only checks committed changes.

Conflicts Between checkpatch and clang-format
----------------------------------------------

``checkpatch`` often can yield seemingly incorrect results due to it not understanding the typedefs in the system.
Our preference is to remove type definitions where possible.
See :ref:`struct-and-enum-definitions` for guidance on preferred definitions.

In cases where ``checkpatch`` and ``clang-format`` disagree on formatting, **checkpatch takes precedence**. When this occurs, you may disable ``clang-format`` for the specific code block that conflicts.

**Example:**

.. code-block:: c

   /* clang-format off */
   static const struct device_config dev_configs[] = {
       { .name = "device1", .addr = 0x1000, .irq = 10 },
       { .name = "device2", .addr = 0x2000, .irq = 11 },
       { .name = "device3", .addr = 0x3000, .irq = 12 },
   };
   /* clang-format on */

Use ``/* clang-format off */`` and ``/* clang-format on */`` comments to disable automatic formatting for blocks where checkpatch formatting is required.

Zephyr Coding Guidelines
========================

Zephyr's coding guidelines are described in the `Zephyr Coding Guidelines <https://docs.zephyrproject.org/latest/contribute/coding_guidelines/index.html>`_.

It is advised to follow the guidelines to the best of your ability, however, some rules are quite difficult to achieve compliance with.

Additional Guidelines
=====================

The following additional guidelines apply to this project:

.. _struct-and-enum-definitions:

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
