#pragma once

#include <QtGlobal>

namespace qt_spy {
namespace protocol {

inline constexpr int kVersion = 1;

namespace keys {
inline constexpr char kType[] = "type";
inline constexpr char kTimestampMs[] = "timestampMs";
inline constexpr char kProtocolVersion[] = "protocolVersion";
inline constexpr char kRequestId[] = "requestId";
inline constexpr char kId[] = "id";
inline constexpr char kParentId[] = "parentId";
inline constexpr char kNode[] = "node";
inline constexpr char kNodes[] = "nodes";
inline constexpr char kRootIds[] = "rootIds";
inline constexpr char kChildIds[] = "childIds";
inline constexpr char kProperties[] = "properties";
inline constexpr char kChanged[] = "changed";
inline constexpr char kSelection[] = "selection";
inline constexpr char kServerName[] = "serverName";
inline constexpr char kApplicationName[] = "applicationName";
inline constexpr char kApplicationPid[] = "applicationPid";
inline constexpr char kClientName[] = "clientName";
} // namespace keys

namespace types {
inline constexpr char kAttach[] = "attach";
inline constexpr char kDetach[] = "detach";
inline constexpr char kHello[] = "hello";
inline constexpr char kGoodbye[] = "goodbye";
inline constexpr char kSnapshotRequest[] = "snapshotRequest";
inline constexpr char kSnapshot[] = "snapshot";
inline constexpr char kPropertiesRequest[] = "propertiesRequest";
inline constexpr char kProperties[] = "properties";
inline constexpr char kSelectNode[] = "selectNode";
inline constexpr char kSelectionAck[] = "selectionAck";
inline constexpr char kNodeAdded[] = "nodeAdded";
inline constexpr char kNodeRemoved[] = "nodeRemoved";
inline constexpr char kPropertiesChanged[] = "propertiesChanged";
inline constexpr char kError[] = "error";
} // namespace types

} // namespace protocol
} // namespace qt_spy
