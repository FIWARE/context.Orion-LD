# Bug report — `FibonacciServer.py` crashes on first `executor.spin_once()` on Jazzy

## Summary

The Python `FibonacciServer.py` script shipped in the FIWARE-DDS-Enabler
test scripts (`ddsenabler_test/compose/scripts/ros2_nodes/`) **terminates
immediately** after announcing the action server. It throws an `RCLError`
from inside `rclpy/executors.py:574` on the *first* `spin_once()` call. The
C++ counterpart (`action_tutorials_cpp fibonacci_action_server` in the
Vulcanexus image) is unaffected.

This was discovered while writing an Orion-LD functional test that uses the
script as the ROS 2 action server peer for testing the broker as a DDS
action *client*. The service-side `AdditionServer.py` (same scripts folder,
service mode) works fine — the failure is specific to the action path.

## Environment

* **Docker image**: `eprosima/vulcanexus:jazzy-desktop`
* **ROS 2 distribution**: Jazzy Jalisco
* **rclpy version**: ships with Jazzy (`/opt/ros/jazzy/lib/python3.12/site-packages/rclpy`)
* **Script location**: `ddsenabler_test/compose/scripts/ros2_nodes/FibonacciServer.py`

## Reproduction

```bash
docker run --rm --ipc=host -e ROS_DOMAIN_ID=0 \
    -v "$PWD/ddsenabler_test/compose/scripts:/scripts:ro" \
    eprosima/vulcanexus:jazzy-desktop \
    python3 -u /scripts/ros2_nodes/node_main.py --action --samples 1
```

## Observed output

```
1778602791.0956993$ Fibonacci Action Server created.
1778602791.0957172$ Running Fibonacci Action Server, waiting for 1 executed goals.
Traceback (most recent call last):
  File "/scripts/ros2_nodes/node_main.py", line 122, in <module>
    main()
  File "/scripts/ros2_nodes/node_main.py", line 108, in main
    result = node.run(args.samples, args.wait, args.expect_cancel)
  File "/scripts/ros2_nodes/FibonacciServer.py", line 137, in run
    executor.spin_once(timeout_sec=0.1)
  File "/opt/ros/jazzy/lib/python3.12/site-packages/rclpy/executors.py", line 917, in spin_once
    self._spin_once_impl(timeout_sec)
  File "/opt/ros/jazzy/lib/python3.12/site-packages/rclpy/executors.py", line 896, in _spin_once_impl
    handler, entity, node = self.wait_for_ready_callbacks(
  File "/opt/ros/jazzy/lib/python3.12/site-packages/rclpy/executors.py", line 799, in wait_for_ready_callbacks
    return next(self._cb_iter)
  File "/opt/ros/jazzy/lib/python3.12/site-packages/rclpy/executors.py", line 574, in _wait_for_ready_callbacks
    timeout_timer = Timer(None, None, timeout_nsec, self._clock, context=self._context)
  File "/opt/ros/jazzy/lib/python3.12/site-packages/rclpy/timer.py", line 60, in __init__
    self.__timer = _rclpy.Timer(
RCLError: failed to create timer: the given context is not valid, either rcl_init() was not called or rcl_shutdown() was called., at ./src/rcl/guard_condition.c:67
```

Note that `Fibonacci Action Server created.` and `Running Fibonacci Action
Server, waiting for 1 executed goals.` are both printed, so:
* `rclpy.init()` *was* called (the node is constructed),
* the executor is built without error,
* the very first `executor.spin_once(timeout_sec=0.1)` raises.

## What seems to be wrong

`MultiThreadedExecutor._wait_for_ready_callbacks` (in `executors.py:574`)
constructs a `Timer` keyed on `self._clock` and `self._context`. Per the
error from `guard_condition.c:67`, `self._context` is reported invalid even
though `rclpy.init()` returned successfully and the node was constructed
against that same default context.

A plausible root cause is a thread-safety / context-binding mismatch
introduced by Jazzy's `rclpy` when a node built with a `ReentrantCallbackGroup`
is then spun on a `MultiThreadedExecutor(num_threads=N)` *without* explicitly
passing the same context the node was created against. The
`AdditionServer.py` script uses the default `MutuallyExclusiveCallbackGroup`
and a single-threaded executor; that path works fine on the same image.

Suggested fixes to validate (any one of these may be enough):

1. Pass the explicit context to the executor:
   ```python
   executor = MultiThreadedExecutor(num_threads=2, context=rclpy.utilities.get_default_context())
   ```
2. Drop the `ReentrantCallbackGroup` (or pass it the node's context).
3. Use `rclpy.spin(node, executor=executor)` instead of the manual
   `spin_once` loop — `rclpy.spin` performs the binding internally.

## Attempted workaround — and why it doesn't work

We tried swapping `FibonacciServer.py` for the canonical
`ros2 run action_tutorials_cpp fibonacci_action_server` from the Vulcanexus
image. The C++ server starts cleanly, accepts goals from `ros2 action
send_goal`, streams feedback, and returns results — **but the broker still
fails to discover it**.

Looking at the broker error:
```
EnablerParticipant.cpp[213]: publish_rpc: Failed to publish data in service
  fibonaccisend_goal : service does not exist.
```

The enabler is looking for a topic named `fibonaccisend_goal`
(literal `<action_name>` + `send_goal` concatenated, no separator),
defined in `Constants.hpp`:
```cpp
constexpr const char* ACTION_GOAL_SUFFIX("send_goal");
constexpr const char* ACTION_CANCEL_SUFFIX("cancel_goal");
constexpr const char* ACTION_RESULT_SUFFIX("get_result");
constexpr const char* ACTION_FEEDBACK_SUFFIX("feedback");
constexpr const char* ACTION_STATUS_SUFFIX("status");
```
and used in `EnablerParticipant.cpp:515`:
```cpp
std::string goal_request_topic = action_name + ACTION_GOAL_SUFFIX;
```

The standard ROS 2 action wire format is
`<action_name>/_action/send_goal` etc. — **completely incompatible** with
the enabler's `<action_name><suffix>` form. So even with the Jazzy
`rclpy` bug fixed, the canonical ROS 2 tutorial servers
(`action_tutorials_cpp`, `action_tutorials_py`,
`examples_rclpy_minimal_action_server`, …) won't interoperate with the
enabler — only `FibonacciServer.py` does, because it was written to
the enabler's convention.

This makes the Python script the **only known action peer** for the
enabler today. Fixing the Jazzy crash is therefore on the critical
path for downstream test suites; we cannot route around it.

## Action requested

1. **Primary** — fix the Jazzy `RCLError: failed to create timer` on the
   first `executor.spin_once()` in `FibonacciServer.py`. The
   `AdditionServer.py` (same scripts folder, single-threaded executor,
   no `ReentrantCallbackGroup`) is unaffected; one of the three
   candidate diffs in the section above should sort it.
2. **Secondary** — consider documenting the action-topic naming
   convention (`<action_name><suffix>`) somewhere visible, and/or
   shipping a C++ companion that uses it. Right now the only working
   action peer is a single Python script that's coupled to a specific
   rclpy version and not exercised by the enabler's own CI.

## Action requested

* Validate the bug on a fresh Vulcanexus `jazzy-desktop` image.
* Apply one of the candidate fixes (or your preferred one) and re-publish
  the test scripts.
* Optionally, add a smoke test in CI that runs `node_main.py --action`
  for a few seconds without sending a goal — the crash above is hit
  before any goal arrives.

## References

* Failing file/line: `ddsenabler_test/compose/scripts/ros2_nodes/FibonacciServer.py:137`
* Working analogue: `ddsenabler_test/compose/scripts/ros2_nodes/AdditionServer.py`
* Working C++ analogue (Vulcanexus): `ros2 run action_tutorials_cpp fibonacci_action_server`
