"""Small ACL/OM runtime wrapper shared by 310B inference modules."""

from __future__ import annotations

from functools import reduce
from operator import mul
import os
import threading
import time
from typing import Any

import numpy as np


NPY_FLOAT32 = 11
ACL_MEMCPY_DEVICE_TO_DEVICE = 3


class _SharedAclRuntime:
    """Reference-count one ACL device context across all OM sessions."""

    _lock = threading.Lock()
    _instances: dict[tuple[int, int], "_SharedAclRuntime"] = {}

    def __init__(self, acl: Any, device_id: int):
        self.acl = acl
        self.device_id = int(device_id)
        self.context = None
        self.refcount = 0
        AclOmSession._check(self.acl.init(), "acl.init")
        try:
            AclOmSession._check(
                self.acl.rt.set_device(self.device_id), "acl.rt.set_device"
            )
            self.context = AclOmSession._value(
                self.acl.rt.create_context(self.device_id),
                "acl.rt.create_context",
            )
        except Exception:
            try:
                self.acl.finalize()
            except Exception:
                pass
            raise

    @classmethod
    def acquire(cls, acl: Any, device_id: int) -> "_SharedAclRuntime":
        key = (id(acl), int(device_id))
        with cls._lock:
            runtime = cls._instances.get(key)
            if runtime is None:
                runtime = cls(acl, device_id)
                cls._instances[key] = runtime
            runtime.refcount += 1
            return runtime

    def set_context(self) -> None:
        if self.context is None:
            raise RuntimeError("shared ACL context is unavailable")
        set_context = getattr(self.acl.rt, "set_context", None)
        if set_context is not None:
            AclOmSession._check(set_context(self.context), "acl.rt.set_context")

    def release(self) -> None:
        key = (id(self.acl), self.device_id)
        with self._lock:
            if self.refcount <= 0:
                return
            self.refcount -= 1
            if self.refcount:
                return
            self._instances.pop(key, None)
            try:
                set_context = getattr(self.acl.rt, "set_context", None)
                if set_context is not None and self.context is not None:
                    set_context(self.context)
            except Exception:
                pass
            if self.context is not None:
                try:
                    self.acl.rt.destroy_context(self.context)
                except Exception:
                    pass
            try:
                reset_device = getattr(self.acl.rt, "reset_device", None)
                if reset_device is not None:
                    reset_device(self.device_id)
            except Exception:
                pass
            try:
                self.acl.finalize()
            except Exception:
                pass
            self.context = None


class AclOmSession:
    """Load and execute one static-input OM model through ACL."""

    def __init__(self, model_path: str, device_id: int = 0, *, input_mode: str = "unspecified"):
        try:
            import acl  # type: ignore
        except ImportError as exc:
            raise RuntimeError(
                "Cannot import acl. Source the Ascend CANN runtime environment "
                "before starting the pipeline."
            ) from exc

        self.acl = acl
        self.model_path = model_path
        self.device_id = device_id
        self.input_mode = input_mode
        self.context = None
        self._runtime: _SharedAclRuntime | None = None
        self.model_id = None
        self.model_desc = None
        self.input_dataset = None
        self.output_dataset = None
        self.input_buffers: list[Any] = []
        self.output_buffers: list[Any] = []
        self.input_device_ptrs: list[Any] = []
        self.output_device_ptrs: list[Any] = []
        self.output_host_ptrs: list[Any] = []
        self.output_host_arrays: list[np.ndarray | None] = []
        self.input_sizes: list[int] = []
        self.output_sizes: list[int] = []
        self.input_dims: list[list[int] | None] = []
        self.output_dims: list[list[int] | None] = []
        self.input_names: list[str | None] = []
        self.output_names: list[str | None] = []
        self.profile_enabled = _env_bool("SMART_IP_ACL_PROFILE", False)
        self.zero_copy_output_enabled = _env_bool("SMART_IP_ACL_ZERO_COPY_OUTPUT", True)
        self.last_profile: dict[str, float] = {}
        self._closed = False

        try:
            self._runtime = _SharedAclRuntime.acquire(self.acl, device_id)
            self.context = self._runtime.context
            self._set_context()
            self.model_id = self._value(self.acl.mdl.load_from_file(model_path), "acl.mdl.load_from_file")
            self.model_desc = self.acl.mdl.create_desc()
            self._check(self.acl.mdl.get_desc(self.model_desc, self.model_id), "acl.mdl.get_desc")

            input_count = int(self._value(self.acl.mdl.get_num_inputs(self.model_desc), "acl.mdl.get_num_inputs"))
            output_count = int(self._value(self.acl.mdl.get_num_outputs(self.model_desc), "acl.mdl.get_num_outputs"))
            if input_count < 1:
                raise RuntimeError(f"OM model has no inputs: {model_path}")
            if output_count < 1:
                raise RuntimeError(f"OM model has no outputs: {model_path}")

            self.input_sizes = [
                int(self._value(self.acl.mdl.get_input_size_by_index(self.model_desc, i), "acl.mdl.get_input_size_by_index"))
                for i in range(input_count)
            ]
            self.output_sizes = [
                int(self._value(self.acl.mdl.get_output_size_by_index(self.model_desc, i), "acl.mdl.get_output_size_by_index"))
                for i in range(output_count)
            ]
            self.input_dims = [self._get_dims("input", i) for i in range(input_count)]
            self.output_dims = [self._get_dims("output", i) for i in range(output_count)]
            self.input_names = [self._get_name("input", i) for i in range(input_count)]
            self.output_names = [self._get_name("output", i) for i in range(output_count)]
            self._create_datasets()
        except Exception:
            self.close()
            raise

    @property
    def runtime_refcount(self) -> int:
        return self._runtime.refcount if self._runtime is not None else 0

    @property
    def shared_runtime(self):
        if self._runtime is None:
            raise RuntimeError("shared ACL runtime is unavailable")
        return self._runtime

    @staticmethod
    def _check(ret: Any, op: str) -> None:
        if isinstance(ret, tuple):
            ret = ret[-1]
        if op == "acl.init" and ret in (100002, 207015):
            return
        if ret not in (0, None):
            raise RuntimeError(f"{op} failed, ret={ret}")

    @classmethod
    def _value(cls, result: Any, op: str) -> Any:
        if isinstance(result, tuple) and len(result) >= 2:
            cls._check(result[-1], op)
            return result[0]
        return result

    def _set_context(self) -> None:
        if self.context is None:
            return
        set_context = getattr(self.acl.rt, "set_context", None)
        if set_context is not None:
            self._check(set_context(self.context), "acl.rt.set_context")

    def _get_dims(self, kind: str, index: int) -> list[int] | None:
        fn_name = f"get_{kind}_dims"
        fn = getattr(self.acl.mdl, fn_name, None)
        if fn is None:
            return None
        try:
            dims = self._value(fn(self.model_desc, index), f"acl.mdl.{fn_name}")
        except Exception:
            return None
        if isinstance(dims, dict):
            values = dims.get("dims")
            count = dims.get("dimCount") or (len(values) if values is not None else 0)
            if values:
                return [int(v) for v in values[:count]]
        if isinstance(dims, (list, tuple)):
            return [int(v) for v in dims]
        return None

    def _get_name(self, kind: str, index: int) -> str | None:
        fn = getattr(self.acl.mdl, f"get_{kind}_name_by_index", None)
        if fn is None:
            return None
        try:
            name = self._value(fn(self.model_desc, index), f"acl.mdl.get_{kind}_name_by_index")
            if isinstance(name, bytes):
                return name.decode("utf-8", "replace")
            return str(name) if name is not None else None
        except Exception:
            return None

    def _create_datasets(self) -> None:
        self.input_dataset = self.acl.mdl.create_dataset()
        self.output_dataset = self.acl.mdl.create_dataset()

        for size in self.input_sizes:
            ptr = self._value(self.acl.rt.malloc(size, 0), "acl.rt.malloc(input)")
            self.input_device_ptrs.append(ptr)
            buf = self.acl.create_data_buffer(ptr, size)
            self.input_buffers.append(buf)
            self._check(self.acl.mdl.add_dataset_buffer(self.input_dataset, buf), "acl.mdl.add_dataset_buffer(input)")

        for size in self.output_sizes:
            dev_ptr = self._value(self.acl.rt.malloc(size, 0), "acl.rt.malloc(output)")
            host_ptr = self._value(self.acl.rt.malloc_host(size), "acl.rt.malloc_host(output)")
            self.output_device_ptrs.append(dev_ptr)
            self.output_host_ptrs.append(host_ptr)
            buf = self.acl.create_data_buffer(dev_ptr, size)
            self.output_buffers.append(buf)
            self._check(self.acl.mdl.add_dataset_buffer(self.output_dataset, buf), "acl.mdl.add_dataset_buffer(output)")

        self._create_output_host_views()

    def _create_output_host_views(self) -> None:
        """Create reusable NumPy views over pinned ACL host output buffers."""
        ptr_to_numpy = getattr(self.acl.util, "ptr_to_numpy", None)
        if not self.zero_copy_output_enabled or ptr_to_numpy is None:
            self.output_host_arrays = [None] * len(self.output_host_ptrs)
            return

        self.output_host_arrays = []
        for idx, (host_ptr, size) in enumerate(zip(self.output_host_ptrs, self.output_sizes)):
            shape = self._output_shape(idx, size)
            try:
                view = ptr_to_numpy(host_ptr, shape, NPY_FLOAT32)
                view = np.asarray(view, dtype=np.float32)
                if view.nbytes != size or view.flags.owndata:
                    raise RuntimeError(
                        f"unexpected ACL host view: nbytes={view.nbytes}, "
                        f"expected={size}, owndata={view.flags.owndata}"
                    )
                self.output_host_arrays.append(view)
            except Exception:
                # Some CANN versions remove/deprecate ptr_to_numpy. Keep the
                # portable bytes path available per output without failing init.
                self.output_host_arrays.append(None)

    def _output_shape(self, index: int, size: int) -> tuple[int, ...]:
        if size % np.dtype(np.float32).itemsize:
            raise RuntimeError(f"OM output {index} is not float32-aligned: {size} bytes")
        dims = self.output_dims[index]
        if dims and prod(dims) * np.dtype(np.float32).itemsize == size:
            return tuple(dims)
        return (size // np.dtype(np.float32).itemsize,)

    def execute(self, input_array: np.ndarray) -> list[np.ndarray]:
        """Execute the model.

        With zero-copy output enabled (the default), returned arrays are views
        over reusable ACL host buffers and remain valid only until this
        session's next ``execute`` call. Set SMART_IP_ACL_ZERO_COPY_OUTPUT=0
        when a caller needs to retain outputs across calls.
        """
        if self._closed:
            raise RuntimeError("ACL OM session is already closed")
        if len(self.input_sizes) != 1:
            raise RuntimeError(f"Only one-input OM models are supported, got {len(self.input_sizes)}")

        total_start = time.perf_counter()
        self._set_context()
        pack_start = time.perf_counter()
        arr = np.ascontiguousarray(input_array)
        input_size = self.input_sizes[0]
        if arr.nbytes != input_size:
            raise RuntimeError(
                "OM input size mismatch: "
                f"model={self.model_path}, dims={self.input_dims[0]}, "
                f"expected_bytes={input_size}, actual_bytes={arr.nbytes}, "
                f"input_mode={self.input_mode}"
            )
        src_ptr, src_owner = self._host_array_ptr(arr)
        pack_ms = _elapsed_ms(pack_start)

        h2d_start = time.perf_counter()
        self._check(
            self.acl.rt.memcpy(self.input_device_ptrs[0], input_size, src_ptr, input_size, 1),
            "acl.rt.memcpy(host_to_device)",
        )
        h2d_ms = _elapsed_ms(h2d_start)
        execute_start = time.perf_counter()
        self._check(self.acl.mdl.execute(self.model_id, self.input_dataset, self.output_dataset), "acl.mdl.execute")
        execute_ms = _elapsed_ms(execute_start)

        outputs = []
        d2h_ms = 0.0
        decode_ms = 0.0
        for idx, (dev_ptr, host_ptr, size) in enumerate(zip(self.output_device_ptrs, self.output_host_ptrs, self.output_sizes)):
            d2h_start = time.perf_counter()
            self._check(
                self.acl.rt.memcpy(host_ptr, size, dev_ptr, size, 2),
                "acl.rt.memcpy(device_to_host)",
            )
            d2h_ms += _elapsed_ms(d2h_start)
            decode_start = time.perf_counter()
            arr_out = self.output_host_arrays[idx]
            if arr_out is None:
                raw = self.acl.util.ptr_to_bytes(host_ptr, size)
                arr_out = np.frombuffer(raw, dtype=np.float32).copy().reshape(self._output_shape(idx, size))
            outputs.append(arr_out)
            decode_ms += _elapsed_ms(decode_start)
        if getattr(self, "profile_enabled", False):
            self.last_profile = {
                "acl_h2d_ms": h2d_ms,
                "acl_execute_ms": execute_ms,
                "acl_d2h_ms": d2h_ms,
                "acl_input_pack_ms": pack_ms,
                "acl_output_decode_ms": decode_ms,
                "acl_total_ms": _elapsed_ms(total_start),
                "acl_h2d_bytes": input_size,
                "acl_d2d_ms": 0.0,
                "acl_d2d_bytes": 0,
            }
        return outputs

    def execute_device_input(self, src_device_ptr, src_size: int, *, copy_mode: str = "d2d") -> list[np.ndarray]:
        """Execute from a DVPP/device pointer using a safe fixed-buffer D2D copy."""
        if self._closed:
            raise RuntimeError("ACL OM session is already closed")
        if len(self.input_sizes) != 1:
            raise RuntimeError(f"Only one-input OM models are supported, got {len(self.input_sizes)}")
        input_size = self.input_sizes[0]
        if int(src_size) != input_size:
            raise RuntimeError(
                "OM input size mismatch: "
                f"model={self.model_path}, dims={self.input_dims[0]}, "
                f"expected_bytes={input_size}, actual_bytes={src_size}, "
                f"input_mode={self.input_mode}"
            )
        if copy_mode != "d2d":
            raise NotImplementedError(
                "external input buffers are disabled until dataset/data-buffer lifetime is board-validated"
            )
        if src_device_ptr in (None, 0):
            raise ValueError("src_device_ptr must be a valid device pointer")
        total_start = time.perf_counter()
        self._set_context()
        d2d_start = time.perf_counter()
        self._check(
            self.acl.rt.memcpy(
                self.input_device_ptrs[0], input_size, src_device_ptr, input_size,
                ACL_MEMCPY_DEVICE_TO_DEVICE,
            ),
            "acl.rt.memcpy(device_to_device)",
        )
        d2d_ms = _elapsed_ms(d2d_start)
        execute_start = time.perf_counter()
        self._check(self.acl.mdl.execute(self.model_id, self.input_dataset, self.output_dataset), "acl.mdl.execute")
        execute_ms = _elapsed_ms(execute_start)
        outputs = []
        d2h_ms = 0.0
        decode_ms = 0.0
        for idx, (dev_ptr, host_ptr, size) in enumerate(zip(self.output_device_ptrs, self.output_host_ptrs, self.output_sizes)):
            d2h_start = time.perf_counter()
            self._check(self.acl.rt.memcpy(host_ptr, size, dev_ptr, size, 2), "acl.rt.memcpy(device_to_host)")
            d2h_ms += _elapsed_ms(d2h_start)
            decode_start = time.perf_counter()
            arr_out = self.output_host_arrays[idx]
            if arr_out is None:
                raw = self.acl.util.ptr_to_bytes(host_ptr, size)
                arr_out = np.frombuffer(raw, dtype=np.float32).copy().reshape(self._output_shape(idx, size))
            outputs.append(arr_out)
            decode_ms += _elapsed_ms(decode_start)
        if self.profile_enabled:
            self.last_profile = {
                "acl_h2d_ms": 0.0, "acl_h2d_bytes": 0,
                "acl_d2d_ms": d2d_ms, "acl_d2d_bytes": input_size,
                "acl_execute_ms": execute_ms, "acl_d2h_ms": d2h_ms,
                "acl_input_pack_ms": 0.0, "acl_output_decode_ms": decode_ms,
                "acl_total_ms": _elapsed_ms(total_start),
            }
        return outputs

    def _host_array_ptr(self, arr: np.ndarray) -> tuple[Any, Any]:
        numpy_to_ptr = getattr(self.acl.util, "numpy_to_ptr", None)
        if numpy_to_ptr is not None:
            return numpy_to_ptr(arr), arr
        input_bytes = arr.tobytes()
        return self.acl.util.bytes_to_ptr(input_bytes), input_bytes

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            self._set_context()
        except Exception:
            pass

        for buf in self.input_buffers + self.output_buffers:
            try:
                self.acl.destroy_data_buffer(buf)
            except Exception:
                pass
        for dataset in (self.input_dataset, self.output_dataset):
            if dataset is not None:
                try:
                    self.acl.mdl.destroy_dataset(dataset)
                except Exception:
                    pass
        # Drop NumPy views before releasing the pinned host memory they expose.
        self.output_host_arrays.clear()
        for ptr in self.output_host_ptrs:
            try:
                self.acl.rt.free_host(ptr)
            except Exception:
                pass
        for ptr in self.input_device_ptrs + self.output_device_ptrs:
            try:
                self.acl.rt.free(ptr)
            except Exception:
                pass
        if self.model_id is not None:
            try:
                self.acl.mdl.unload(self.model_id)
            except Exception:
                pass
        if self.model_desc is not None:
            try:
                self.acl.mdl.destroy_desc(self.model_desc)
            except Exception:
                pass
        runtime, self._runtime = self._runtime, None
        if runtime is not None:
            runtime.release()
        self.context = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


def prod(values: list[int] | tuple[int, ...] | None) -> int:
    if not values:
        return 0
    return int(reduce(mul, values, 1))


def _env_bool(name: str, default: bool) -> bool:
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() not in ("0", "false", "no", "off")


def _elapsed_ms(start: float) -> float:
    return (time.perf_counter() - start) * 1000.0
