// ==============================================================================
// c28x_uint8_compat.h — C28x용 uint8_t 호환 shim
//
// C28x는 하드웨어에 8비트 타입이 없어(최소 addressable unit이 16비트) TI stdint.h가
// 이 아키텍처에서는 uint8_t/int8_t를 아예 정의하지 않습니다. DriverLib을 포함하면
// device/driverlib/inc/hw_types.h가 동일한 shim(uint8_t = uint16_t)을 제공하지만,
// 이 파일(virtual_disk.h 등)은 DriverLib 없이도 독립적으로 컴파일될 수 있도록
// 설계되어 있어 여기서 직접 shim을 둡니다. typedef를 두 번 선언해도(동일 타입이면)
// C 표준상 오류가 아니므로 driverlib.h와 함께 include돼도 안전합니다.
// ==============================================================================
#ifndef C28X_UINT8_COMPAT_H
#define C28X_UINT8_COMPAT_H

#include <stdint.h>

#if defined(__TMS320C2000__) && !defined(__TMS320C28XX_CLA__)
#if !defined(HW_TYPES_H) && !defined(__HW_TYPES_H__)
typedef uint16_t uint8_t;
typedef int16_t  int8_t;
#endif
#endif

#endif // C28X_UINT8_COMPAT_H
