# SingLilt assets

The product name, executable, Qt application identity and release packages use **SingLilt**. Project-owned C++ uses the `singlilt` namespace. The JPP extension and file format are unchanged.

On the first normal startup, SingLilt imports missing preferences, practice history and staff-edit recovery files from the old `JianpuPlayer` identity. Existing SingLilt values and files win; the original state is left untouched. The import is recorded only after all writes succeed. Command-line and isolated diagnostic runs do not import user state.

The existing `JIANPU_*` environment variables remain supported for compatibility with local audio and recognition setups. They are not the application identity.

The blue-purple icon combines a musical note with a rising melody ribbon. The runtime PNG and Windows ICO are in `resources/branding`; the Windows version resource is `SingLilt.rc`.

The icon master was generated with OpenAI ImageGen. PNG/ICO variants are derived from that master. Project-owned assets are distributed under the project license; third-party fonts keep their own license.
