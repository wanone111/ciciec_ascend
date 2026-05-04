"""Small ACL/OM runtime wrapper shared by 310B inference modules."""

from __future__ import annotations

from functools import reduce
from operator import mul
from typing import Any

import numpy as np


class AclOmSession:
    """Load and execute one static-input OM model through ACL."""

    def __init__(self, model_path: str, device_id: int = 0):
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
        self.context = None
        self.model_id = None
        self.model_desc = None
        self.input_dataset = None
        self.output_dataset = None
        self.input_buffers: list[Any] = []
        self.output_buffers: list[Any] = []
        self.input_device_ptrs: list[Any] = []
        self.output_device_ptrs: list[Any] = []
        self.output_host_ptrs: list[Any] = []
        self.input_sizes: list[int] = []
        self.output_sizes: list[int] = []
        self.input_dims: list[list[int] | None] = []
        self.output_dims: list[list[int] | None] = []
        self._closed = False

        self._check(self.acl.init(), "acl.init")
        self._check(self.acl.rt.set_device(device_id), "acl.rt.set_device")
        self.context = self._value(self.acl.rt.create_context(device_id), "acl.rt.create_context")
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
        self._create_datasets()

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

    def execute(self, input_array: np.ndarray) -> list[np.ndarray]:
        if self._closed:
            raise RuntimeError("ACL OM session is already closed")
        if len(self.input_sizes) != 1:
            raise RuntimeError(f"Only one-input OM models are supported, got {len(self.input_sizes)}")

        self._set_context()
        arr = np.ascontiguousarray(input_array)
        input_bytes = arr.tobytes()
        input_size = self.input_sizes[0]
        if len(input_bytes) != input_size:
            raise RuntimeError(
                f"OM input size mismatch: model expects {input_size} bytes, "
                f"preprocess produced {len(input_bytes)} bytes."
            )

        src_ptr = self.acl.util.bytes_to_ptr(input_bytes)
        self._check(
            self.acl.rt.memcpy(self.input_device_ptrs[0], input_size, src_ptr, input_size, 1),
            "acl.rt.memcpy(host_to_device)",
        )
        self._check(self.acl.mdl.execute(self.model_id, self.input_dataset, self.output_dataset), "acl.mdl.execute")

        outputs = []
        for idx, (dev_ptr, host_ptr, size) in enumerate(zip(self.output_device_ptrs, self.output_host_ptrs, self.output_sizes)):
            self._check(
                self.acl.rt.memcpy(host_ptr, size, dev_ptr, size, 2),
                "acl.rt.memcpy(device_to_host)",
            )
            raw = self.acl.util.ptr_to_bytes(host_ptr, size)
            arr_out = np.frombuffer(raw, dtype=np.float32).copy()
            dims = self.output_dims[idx]
            if dims:
                elem_count = prod(dims)
                if elem_count == arr_out.size:
                    arr_out = arr_out.reshape(dims)
            outputs.append(arr_out)
        return outputs

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
        if self.context is not None:
            try:
                self.acl.rt.destroy_context(self.context)
            except Exception:
                pass

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


def prod(values: list[int] | tuple[int, ...] | None) -> int:
    if not values:
        return 0
    return int(reduce(mul, values, 1))
