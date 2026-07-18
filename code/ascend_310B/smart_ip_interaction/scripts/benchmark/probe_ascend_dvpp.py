#!/usr/bin/env python3
"""Print board CANN/ACL/DVPP capabilities without changing device state."""

import json
import platform


def main():
    result = {"python": platform.python_version()}
    try:
        import acl
        result["acl_imported"] = True
        result["acl_module"] = repr(acl)
        names = [
            "dvpp_malloc", "dvpp_free", "dvpp_vpc_resize_async",
            "dvpp_vpc_crop_and_paste_async", "dvpp_create_channel_desc",
            "dvpp_create_channel", "dvpp_create_pic_desc",
            "dvpp_create_roi_config", "dvpp_create_resize_config",
        ]
        result["media_apis"] = {name: hasattr(acl.media, name) for name in names}
        result["nv12_enums"] = {
            name: getattr(acl.media, name, None)
            for name in ("PIXEL_FORMAT_YUV_SEMIPLANAR_420", "PIXEL_FORMAT_YUV_420_SP")
        }
        result["pixel_related_names"] = {
            "acl": [name for name in dir(acl) if "PIXEL" in name or "YUV" in name],
            "acl.media": [name for name in dir(acl.media) if "PIXEL" in name or "YUV" in name],
        }
        result["api_docs"] = {
            name: getattr(getattr(acl.media, name, None), "__doc__", None)
            for name in (
                "dvpp_create_roi_config", "dvpp_vpc_crop_and_paste_async",
                "dvpp_set_pic_desc_data", "dvpp_malloc",
            )
        }
    except Exception as exc:
        result["acl_imported"] = False
        result["error"] = repr(exc)
    print(json.dumps(result, ensure_ascii=False, indent=2, default=str))


if __name__ == "__main__":
    main()
