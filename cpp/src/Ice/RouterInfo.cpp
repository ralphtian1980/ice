// **********************************************************************
//
// Copyright (c) 2003-2010 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Ice/RouterInfo.h>
#include <Ice/Router.h>
#include <Ice/LocalException.h>
#include <Ice/Connection.h> // For ice_connection()->timeout().
#include <Ice/Functional.h>
#include <Ice/Reference.h>

using namespace std;
using namespace Ice;
using namespace IceInternal;

IceUtil::Shared* IceInternal::upCast(RouterManager* p) { return p; }
IceUtil::Shared* IceInternal::upCast(RouterInfo* p) { return p; }

IceInternal::RouterManager::RouterManager() :
    _tableHint(_table.end())
{
}

void
IceInternal::RouterManager::destroy()
{
    IceUtil::Mutex::Lock sync(*this);

    for_each(_table.begin(), _table.end(), Ice::secondVoidMemFun<const RouterPrx, RouterInfo>(&RouterInfo::destroy));

    _table.clear();
    _tableHint = _table.end();
}

RouterInfoPtr
IceInternal::RouterManager::get(const RouterPrx& rtr)
{
    if(!rtr)
    {
        return 0;
    }

    RouterPrx router = RouterPrx::uncheckedCast(rtr->ice_router(0)); // The router cannot be routed.

    IceUtil::Mutex::Lock sync(*this);

    map<RouterPrx, RouterInfoPtr>::iterator p = _table.end();
    
    if(_tableHint != _table.end())
    {
        if(_tableHint->first == router)
        {
            p = _tableHint;
        }
    }
    
    if(p == _table.end())
    {
        p = _table.find(router);
    }

    if(p == _table.end())
    {
        _tableHint = _table.insert(_tableHint, pair<const RouterPrx, RouterInfoPtr>(router, new RouterInfo(router)));
    }
    else
    {
        _tableHint = p;
    }

    return _tableHint->second;
}

RouterInfoPtr
IceInternal::RouterManager::erase(const RouterPrx& rtr)
{
    RouterInfoPtr info;
    if(rtr)
    {
        RouterPrx router = RouterPrx::uncheckedCast(rtr->ice_router(0)); // The router cannot be routed.
        IceUtil::Mutex::Lock sync(*this);

        map<RouterPrx, RouterInfoPtr>::iterator p = _table.end();
        if(_tableHint != _table.end() && _tableHint->first == router)
        {
            p = _tableHint;
            _tableHint = _table.end();
        }
        
        if(p == _table.end())
        {
            p = _table.find(router);
        }
        
        if(p != _table.end())
        {
            info = p->second;
            _table.erase(p);
        }
    }

    return info;
}

IceInternal::RouterInfo::RouterInfo(const RouterPrx& router) :
    _router(router)
{
    assert(_router);
}

void
IceInternal::RouterInfo::destroy()
{
    IceUtil::Mutex::Lock sync(*this);

    _clientEndpoints.clear();
    _serverEndpoints.clear();
    _adapter = 0;
    _identities.clear();
}

bool
IceInternal::RouterInfo::operator==(const RouterInfo& rhs) const
{
    return _router == rhs._router;
}

bool
IceInternal::RouterInfo::operator!=(const RouterInfo& rhs) const
{
    return _router != rhs._router;
}

bool
IceInternal::RouterInfo::operator<(const RouterInfo& rhs) const
{
    return _router < rhs._router;
}

RouterPrx
IceInternal::RouterInfo::getRouter() const
{
    //
    // No mutex lock necessary, _router is immutable.
    //
    return _router;
}

vector<EndpointIPtr>
IceInternal::RouterInfo::getClientEndpoints()
{
    {
        IceUtil::Mutex::Lock sync(*this);
        if(!_clientEndpoints.empty())
        {
            return _clientEndpoints;
        }
    }

    return setClientEndpoints(_router->getClientProxy());
}

void
IceInternal::RouterInfo::getClientEndpoints(const GetClientEndpointsCallbackPtr& callback)
{
    vector<EndpointIPtr> clientEndpoints;
    {
        IceUtil::Mutex::Lock sync(*this);
        clientEndpoints = _clientEndpoints;
    }

    if(!clientEndpoints.empty())
    {
        callback->setEndpoints(clientEndpoints);
        return;
    }

    class Callback : public AMI_Router_getClientProxy
    {
    public:

        virtual void
        ice_response(const Ice::ObjectPrx& clientProxy)
        {
            _callback->setEndpoints(_routerInfo->setClientEndpoints(clientProxy));
        }

        virtual void
        ice_exception(const Ice::Exception& ex)
        {
            if(dynamic_cast<const Ice::CollocationOptimizationException*>(&ex))
            {
                try
                {
                    _callback->setEndpoints(_routerInfo->getClientEndpoints());
                }
                catch(const Ice::LocalException& e)
                {
                    _callback->setException(e);
                }
            }
            else
            {
                _callback->setException(dynamic_cast<const Ice::LocalException&>(ex));
            }
        }

        Callback(const RouterInfoPtr& routerInfo, const GetClientEndpointsCallbackPtr& callback) :
            _routerInfo(routerInfo), _callback(callback)
        {
        }

    private:

        const RouterInfoPtr _routerInfo;
        const GetClientEndpointsCallbackPtr _callback;
    };

    _router->getClientProxy_async(new Callback(this, callback));
}

vector<EndpointIPtr>
IceInternal::RouterInfo::getServerEndpoints()
{
    {
        IceUtil::Mutex::Lock sync(*this);
        if(!_serverEndpoints.empty())
        {
            return _serverEndpoints;
        }
    }

    return setServerEndpoints(_router->getServerProxy());
}

void
IceInternal::RouterInfo::addProxy(const ObjectPrx& proxy)
{
    assert(proxy); // Must not be called for null proxies.

    {
        IceUtil::Mutex::Lock sync(*this);
        if(_identities.find(proxy->ice_getIdentity()) != _identities.end())
        {
            //
            // Only add the proxy to the router if it's not already in our local map.
            //
            return;
        }
    }

    ObjectProxySeq proxies;
    proxies.push_back(proxy);
    addAndEvictProxies(proxy, _router->addProxies(proxies));
}

bool
IceInternal::RouterInfo::addProxy(const Ice::ObjectPrx& proxy, const AddProxyCallbackPtr& callback)
{
    assert(proxy);
    {
        IceUtil::Mutex::Lock sync(*this);
        if(_identities.find(proxy->ice_getIdentity()) != _identities.end())
        {
            //
            // Only add the proxy to the router if it's not already in our local map.
            //
            return true;
        }
    }

    class Callback : public AMI_Router_addProxies
    {
    public:
        
        virtual void
        ice_response(const Ice::ObjectProxySeq& evictedProxies)
        {
            _routerInfo->addAndEvictProxies(_proxy, evictedProxies);
            _callback->addedProxy();
        }

        virtual void
        ice_exception(const Ice::Exception& ex)
        {
            if(dynamic_cast<const Ice::CollocationOptimizationException*>(&ex))
            {
                try
                {
                    _routerInfo->addProxy(_proxy);
                    _callback->addedProxy();
                }
                catch(const Ice::LocalException& e)
                {
                    _callback->setException(e);
                }
            }
            else
            {
                _callback->setException(dynamic_cast<const Ice::LocalException&>(ex));
            }
        }

        Callback(const RouterInfoPtr& routerInfo, const Ice::ObjectPrx& proxy, const AddProxyCallbackPtr& callback) :
            _routerInfo(routerInfo), _proxy(proxy), _callback(callback)
        {
        }
        
    private:
        
        const RouterInfoPtr _routerInfo;
        const Ice::ObjectPrx _proxy;
        const AddProxyCallbackPtr _callback;
    };

    Ice::ObjectProxySeq proxies;
    proxies.push_back(proxy);
    _router->addProxies_async(new Callback(this, proxy, callback), proxies);
    return false;
}

void
IceInternal::RouterInfo::setAdapter(const ObjectAdapterPtr& adapter)
{
    IceUtil::Mutex::Lock sync(*this);
    _adapter = adapter;
}

ObjectAdapterPtr
IceInternal::RouterInfo::getAdapter() const
{
    IceUtil::Mutex::Lock sync(*this);
    return _adapter;
}

void
IceInternal::RouterInfo::clearCache(const ReferencePtr& ref)
{
    IceUtil::Mutex::Lock sync(*this);
    _identities.erase(ref->getIdentity());
}

vector<EndpointIPtr>
IceInternal::RouterInfo::setClientEndpoints(const Ice::ObjectPrx& proxy)
{
    IceUtil::Mutex::Lock sync(*this);
    if(_clientEndpoints.empty())
    {
        if(!proxy)
        {
            //
            // If getClientProxy() return nil, use router endpoints.
            //
            _clientEndpoints = _router->__reference()->getEndpoints();
        }
        else
        {
            Ice::ObjectPrx clientProxy = proxy->ice_router(0); // The client proxy cannot be routed.

            //
            // In order to avoid creating a new connection to the router,
            // we must use the same timeout as the already existing
            // connection.
            //
            try
            {
                clientProxy = clientProxy->ice_timeout(_router->ice_getConnection()->timeout());
            }
            catch(const Ice::CollocationOptimizationException&)
            {
                // Ignore - collocated router
            }

            _clientEndpoints = clientProxy->__reference()->getEndpoints();
        }
    }
    return _clientEndpoints;
}


vector<EndpointIPtr>
IceInternal::RouterInfo::setServerEndpoints(const Ice::ObjectPrx& serverProxy)
{
    IceUtil::Mutex::Lock sync(*this);
    if(_serverEndpoints.empty()) // Lazy initialization.
    {
        ObjectPrx serverProxy = _router->getServerProxy();
        if(!serverProxy)
        {
            throw NoEndpointException(__FILE__, __LINE__);
        }

        serverProxy = serverProxy->ice_router(0); // The server proxy cannot be routed.

        _serverEndpoints = serverProxy->__reference()->getEndpoints();
    }
    return _serverEndpoints;
}

void
IceInternal::RouterInfo::addAndEvictProxies(const Ice::ObjectPrx& proxy, const Ice::ObjectProxySeq& evictedProxies)
{
    IceUtil::Mutex::Lock sync(*this);

    //
    // Check if the proxy hasn't already been evicted by a concurrent addProxies call. 
    // If it's the case, don't add it to our local map.
    //
    multiset<Identity>::iterator p = _evictedIdentities.find(proxy->ice_getIdentity());
    if(p != _evictedIdentities.end())
    {
        _evictedIdentities.erase(p);
    }
    else
    {
        //
        // If we successfully added the proxy to the router,
        // we add it to our local map.
        //
        _identities.insert(proxy->ice_getIdentity());
    }
    
    //
    // We also must remove whatever proxies the router evicted.
    //
    for(Ice::ObjectProxySeq::const_iterator q = evictedProxies.begin(); q != evictedProxies.end(); ++q)
    {
        if(_identities.erase((*q)->ice_getIdentity()) == 0)
        {
            //
            // It's possible for the proxy to not have been
            // added yet in the local map if two threads
            // concurrently call addProxies.
            //
            _evictedIdentities.insert((*q)->ice_getIdentity());
        }
    }
}
