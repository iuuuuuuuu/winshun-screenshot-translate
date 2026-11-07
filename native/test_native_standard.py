"""Run native QA with WinShun-style restricted medium rights on a private desktop.

Start this developer harness from an elevated shell. It never changes the user's
desktop or input and never modifies an installed application's access control.
Arguments are forwarded to test_native.py, including --build and --output.
"""
import ctypes

import test_native as qa
from test_native import api, w


class SidAttributes(ctypes.Structure):
    _fields_ = [('sid', ctypes.c_void_p), ('attributes', w.DWORD)]


class SecurityAttributes(ctypes.Structure):
    _fields_ = [('size', w.DWORD), ('descriptor', ctypes.c_void_p), ('inherit', w.BOOL)]


def checked(result):
    if not result:
        raise ctypes.WinError()
    return result


def main():
    original_api = qa.api
    original_launch = qa.launch
    handles, allocations = [], []
    try:
        own, restricted = w.HANDLE(), w.HANDLE()
        checked(api('advapi32', 'OpenProcessToken', w.BOOL, w.HANDLE, w.DWORD, ctypes.POINTER(w.HANDLE))(
            api('kernel32', 'GetCurrentProcess', w.HANDLE)(), 0x8B, ctypes.byref(own)))
        handles.append(own)
        get_info = api('advapi32', 'GetTokenInformation', w.BOOL, w.HANDLE, ctypes.c_int,
                       ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD))
        elevated, size = w.DWORD(), w.DWORD()
        checked(get_info(own, 20, ctypes.byref(elevated), ctypes.sizeof(elevated), ctypes.byref(size)))
        assert elevated.value, 'Run this harness from an elevated developer shell'

        def sid(value):
            result = ctypes.c_void_p()
            checked(api('advapi32', 'ConvertStringSidToSidW', w.BOOL, w.LPCWSTR,
                        ctypes.POINTER(ctypes.c_void_p))(value, ctypes.byref(result)))
            allocations.append(result)
            return result

        # Same disabled groups, privilege removal and integrity as WinShun's
        # Sandbox.cpp. Merely lowering integrity leaves administrator access.
        denied = (SidAttributes * 2)(SidAttributes(sid('S-1-5-32-544'), 0), SidAttributes(sid('S-1-5-114'), 0))
        checked(api('advapi32', 'CreateRestrictedToken', w.BOOL, w.HANDLE, w.DWORD, w.DWORD,
                    ctypes.c_void_p, w.DWORD, ctypes.c_void_p, w.DWORD, ctypes.c_void_p, ctypes.POINTER(w.HANDLE))(
            own, 1, 2, denied, 0, None, 0, None, ctypes.byref(restricted)))
        handles.append(restricted)
        label = sid('S-1-16-8192')
        mandatory = SidAttributes(label, 0x20)
        set_info = api('advapi32', 'SetTokenInformation', w.BOOL, w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD)
        checked(set_info(restricted, 25, ctypes.byref(mandatory), ctypes.sizeof(mandatory) +
                         api('advapi32', 'GetLengthSid', w.DWORD, ctypes.c_void_p)(label)))

        user = ctypes.create_string_buffer(256)
        checked(get_info(own, 1, user, len(user), ctypes.byref(size)))
        user_sid = ctypes.cast(user, ctypes.POINTER(SidAttributes)).contents.sid
        user_string = ctypes.c_wchar_p()
        checked(api('advapi32', 'ConvertSidToStringSidW', w.BOOL, ctypes.c_void_p,
                    ctypes.POINTER(ctypes.c_wchar_p))(user_sid, ctypes.byref(user_string)))
        allocations.append(user_string)
        descriptor = ctypes.c_void_p()
        checked(api('advapi32', 'ConvertStringSecurityDescriptorToSecurityDescriptorW', w.BOOL,
                    w.LPCWSTR, w.DWORD, ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p)(
            'D:(A;OICI;GA;;;SY)(A;OICI;GA;;;' + user_string.value + ')S:(ML;OICI;NW;;;ME)', 1,
            ctypes.byref(descriptor), None))
        allocations.append(descriptor)
        present, defaulted, acl = w.BOOL(), w.BOOL(), ctypes.c_void_p()
        checked(api('advapi32', 'GetSecurityDescriptorDacl', w.BOOL, ctypes.c_void_p,
                    ctypes.POINTER(w.BOOL), ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(w.BOOL))(
            descriptor, ctypes.byref(present), ctypes.byref(acl), ctypes.byref(defaulted)))
        # The restricted child needs its own user in the default DACL to load
        # DLLs and create objects after Administrators became deny-only.
        checked(set_info(restricted, 6, ctypes.byref(acl), ctypes.sizeof(acl)))
        security = SecurityAttributes(ctypes.sizeof(SecurityAttributes), descriptor, False)
        sacl = ctypes.c_void_p()
        checked(api('advapi32', 'GetSecurityDescriptorSacl', w.BOOL, ctypes.c_void_p,
                    ctypes.POINTER(w.BOOL), ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(w.BOOL))(
            descriptor, ctypes.byref(present), ctypes.byref(sacl), ctypes.byref(defaulted)))

        def standard_launch(program, args, data, output, *options):
            # Python creates its temporary directory with an administrator-only
            # ACL in an elevated shell. Give this test's own fixtures to the
            # same user at medium integrity, like the real plugin data folder.
            output.mkdir(parents=True, exist_ok=True)
            for path in [data, *data.rglob('*'), output]:
                assert not path.is_symlink(), path
                error = api('advapi32', 'SetNamedSecurityInfoW', w.DWORD, w.LPWSTR, ctypes.c_int,
                            w.DWORD, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)(
                    str(path.resolve()), 1, 4 | 0x10, None, None, acl, sacl)
                if error:
                    raise ctypes.WinError(error)
            original_launch(program, args, data, output, *options)
            if '--probe' in args:
                assert 'selection-reader=standard' in (output/'stderr.log').read_text(encoding='utf-8')

        def standard_api(library, function, result, *types):
            if (library, function) == ('kernel32', 'CreateProcessW'):
                create = original_api('advapi32', 'CreateProcessAsUserW', result, w.HANDLE, *types)
                return lambda *values: create(restricted, *values)
            if (library, function) == ('user32', 'CreateDesktopW'):
                create = original_api(library, function, result, *types)
                return lambda *values: create(*values[:-1], ctypes.byref(security))
            return original_api(library, function, result, *types)

        qa.api = standard_api
        qa.launch = standard_launch
        qa.main()
    finally:
        qa.api = original_api
        qa.launch = original_launch
        for handle in reversed(handles):
            api('kernel32', 'CloseHandle', w.BOOL, w.HANDLE)(handle)
        for allocation in reversed(allocations):
            api('kernel32', 'LocalFree', ctypes.c_void_p, ctypes.c_void_p)(allocation)


if __name__ == '__main__':
    main()
