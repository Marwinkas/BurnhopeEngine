#pragma once

// SAL compatibility header for Linux
// Defines SAL macros as empty to allow DirectXMath compilation

#ifndef _WIN32

#define _In_reads_(x)
#define _In_reads_opt_(x)
#define _Out_writes_(x)
#define _Out_writes_opt_(x)
#define _Inout_updates_(x)
#define _Inout_updates_opt_(x)
#define _Inout_updates_bytes_(x)
#define _Inout_updates_bytes_opt_(x)
#define _Inout_updates_z_(x)
#define _Inout_updates_z_opt_(x)
#define _Inout_updates_all_(x)
#define _Inout_updates_all_opt_(x)
#define _Out_writes_bytes_(x)
#define _In_reads_bytes_(x)
#define _Use_decl_annotations_
#define _Analysis_assume_(x)
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
#define _Post_
#define _Check_return_
#define _Success_(x)
#define _Reserved_
#define _Const_

#endif // !_WIN32
