// **********************************************************************
//
// Copyright (c) 2003-2016 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#pragma once

[["cpp:header-ext:h", "objc:header-dir:objc"]]

#include <Ice/Connection.ice>
#include <Ice/JavaCompat.ice>

#ifndef JAVA_COMPAT
[["java:package:com.zeroc"]]
#endif

["objc:prefix:ICEBT"]
module IceBT
{

/**
 *
 * Provides access to the details of a Bluetooth connection.
 *
 **/
local class ConnectionInfo extends Ice::ConnectionInfo
{
    /** The local Bluetooth address. */
    string localAddress = "";

    /** The local RFCOMM channel. */
    int localChannel = -1;

    /** The remote Bluetooth address. */
    string remoteAddress = "";

    /** The remote RFCOMM channel. */
    int remoteChannel = -1;

    /** The UUID of the service being offered (in a server) or targeted (in a client). */
    string uuid = "";

    /** The connection buffer receive size. **/
    int rcvSize = 0;

    /** The connection buffer send size. **/
    int sndSize = 0;
};

};
