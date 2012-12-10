# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from WebIDL import IDLInterface, IDLExternalInterface

autogenerated_comment = "/* THIS FILE IS AUTOGENERATED - DO NOT EDIT */\n"

class Configuration:
    """
    Represents global configuration state based on IDL parse data and
    the configuration file.
    """
    def __init__(self, filename, parseData):

        # Read the configuration file.
        glbl = {}
        execfile(filename, glbl)
        config = glbl['DOMInterfaces']

        # Build descriptors for all the interfaces we have in the parse data.
        # This allows callers to specify a subset of interfaces by filtering
        # |parseData|.
        self.descriptors = []
        self.interfaces = {}
        self.maxProtoChainLength = 0;
        for thing in parseData:
            # Some toplevel things are sadly types, and those have an
            # isInterface that doesn't mean the same thing as IDLObject's
            # isInterface()...
            if (not isinstance(thing, IDLInterface) and
                not isinstance(thing, IDLExternalInterface)):
                continue
            iface = thing
            self.interfaces[iface.identifier.name] = iface
            if iface.identifier.name not in config:
                # Completely skip consequential interfaces with no descriptor
                # because chances are we don't need to do anything interesting
                # with them.
                if iface.isConsequential():
                    continue
                entry = {}
            else:
                entry = config[iface.identifier.name]
            if not isinstance(entry, list):
                assert isinstance(entry, dict)
                entry = [entry]
            elif len(entry) == 1 and entry[0].get("workers", False):
                # List with only a workers descriptor means we should
                # infer a mainthread descriptor.  If you want only
                # workers bindings, don't use a list here.
                entry.append({})
            self.descriptors.extend([Descriptor(self, iface, x) for x in entry])

        # Mark the descriptors for which only a single nativeType implements
        # an interface.
        for descriptor in self.descriptors:
            intefaceName = descriptor.interface.identifier.name
            otherDescriptors = [d for d in self.descriptors
                                if d.interface.identifier.name == intefaceName]
            descriptor.uniqueImplementation = len(otherDescriptors) == 1

        self.enums = [e for e in parseData if e.isEnum()]
        self.dictionaries = [d for d in parseData if d.isDictionary()]
        self.callbacks = [c for c in parseData if
                          c.isCallback() and not c.isInterface()]

        # Keep the descriptor list sorted for determinism.
        self.descriptors.sort(lambda x,y: cmp(x.name, y.name))

    def getInterface(self, ifname):
        return self.interfaces[ifname]
    def getDescriptors(self, **filters):
        """Gets the descriptors that match the given filters."""
        curr = self.descriptors
        for key, val in filters.iteritems():
            if key == 'webIDLFile':
                getter = lambda x: x.interface.filename()
            elif key == 'hasInterfaceObject':
                getter = lambda x: (not x.interface.isExternal() and
                                    x.interface.hasInterfaceObject())
            elif key == 'hasInterfacePrototypeObject':
                getter = lambda x: (not x.interface.isExternal() and
                                    x.interface.hasInterfacePrototypeObject())
            elif key == 'hasInterfaceOrInterfacePrototypeObject':
                getter = lambda x: x.hasInterfaceOrInterfacePrototypeObject()
            elif key == 'isCallback':
                getter = lambda x: x.interface.isCallback()
            elif key == 'isExternal':
                getter = lambda x: x.interface.isExternal()
            else:
                getter = lambda x: getattr(x, key)
            curr = filter(lambda x: getter(x) == val, curr)
        return curr
    def getEnums(self, webIDLFile):
        return filter(lambda e: e.filename() == webIDLFile, self.enums)
    def getDictionaries(self, webIDLFile=None):
        if not webIDLFile:
            return self.dictionaries
        return filter(lambda d: d.filename() == webIDLFile, self.dictionaries)
    def getCallbacks(self, webIDLFile=None):
        if not webIDLFile:
            return self.callbacks
        return filter(lambda d: d.filename() == webIDLFile, self.callbacks)
    def getDescriptor(self, interfaceName, workers):
        """
        Gets the appropriate descriptor for the given interface name
        and the given workers boolean.
        """
        iface = self.getInterface(interfaceName)
        descriptors = self.getDescriptors(interface=iface)

        # The only filter we currently have is workers vs non-workers.
        matches = filter(lambda x: x.workers is workers, descriptors)

        # After filtering, we should have exactly one result.
        if len(matches) is not 1:
            raise NoSuchDescriptorError("For " + interfaceName + " found " +
                                        str(len(matches)) + " matches");
        return matches[0]
    def getDescriptorProvider(self, workers):
        """
        Gets a descriptor provider that can provide descriptors as needed,
        for the given workers boolean
        """
        return DescriptorProvider(self, workers)

class NoSuchDescriptorError(TypeError):
    def __init__(self, str):
        TypeError.__init__(self, str)

class DescriptorProvider:
    """
    A way of getting descriptors for interface names
    """
    def __init__(self, config, workers):
        self.config = config
        self.workers = workers

    def getDescriptor(self, interfaceName):
        """
        Gets the appropriate descriptor for the given interface name given the
        context of the current descriptor. This selects the appropriate
        implementation for cases like workers.
        """
        return self.config.getDescriptor(interfaceName, self.workers)

class Descriptor(DescriptorProvider):
    """
    Represents a single descriptor for an interface. See Bindings.conf.
    """
    def __init__(self, config, interface, desc):
        DescriptorProvider.__init__(self, config, desc.get('workers', False))
        self.interface = interface

        # Read the desc, and fill in the relevant defaults.
        ifaceName = self.interface.identifier.name
        if self.interface.isExternal() or self.interface.isCallback():
            if self.workers:
                nativeTypeDefault = "JSObject"
            else:
                nativeTypeDefault = "nsIDOM" + ifaceName
        else:
            if self.workers:
                nativeTypeDefault = "mozilla::dom::workers::" + ifaceName
            else:
                nativeTypeDefault = "mozilla::dom::" + ifaceName

        self.nativeType = desc.get('nativeType', nativeTypeDefault)
        self.hasInstanceInterface = desc.get('hasInstanceInterface', None)

        # Do something sane for JSObject
        if self.nativeType == "JSObject":
            headerDefault = "jsapi.h"
        else:
            if self.workers:
                headerDefault = "mozilla/dom/workers/bindings/%s.h" % ifaceName
            else:
                headerDefault = self.nativeType
                headerDefault = headerDefault.replace("::", "/") + ".h"
        self.headerFile = desc.get('headerFile', headerDefault)

        self.skipGen = desc.get('skipGen', False)

        if (self.interface.isCallback() or self.interface.isExternal() or
            self.skipGen):
            if 'castable' in desc:
                raise TypeError("%s is external or callback or skipGen but has "
                                "a castable setting" %
                                self.interface.identifier.name)
            self.castable = False
        else:
            self.castable = desc.get('castable', True)

        self.notflattened = desc.get('notflattened', False)
        self.register = desc.get('register', True)

        self.hasXPConnectImpls = desc.get('hasXPConnectImpls', False)

        # If we're concrete, we need to crawl our ancestor interfaces and mark
        # them as having a concrete descendant.
        self.concrete = (not self.interface.isExternal() and
                         not self.interface.isCallback() and
                         desc.get('concrete', True))
        operations = {
            'IndexedGetter': None,
            'IndexedSetter': None,
            'IndexedCreator': None,
            'IndexedDeleter': None,
            'NamedGetter': None,
            'NamedSetter': None,
            'NamedCreator': None,
            'NamedDeleter': None,
            'Stringifier': None
            }
        if self.concrete:
            self.proxy = False
            iface = self.interface
            def addOperation(operation, m):
                if not operations[operation]:
                    operations[operation] = m
            # Since stringifiers go on the prototype, we only need to worry
            # about our own stringifier, not those of our ancestor interfaces.
            for m in iface.members:
                if m.isMethod() and m.isStringifier():
                    addOperation('Stringifier', m)
            while iface:
                for m in iface.members:
                    if not m.isMethod():
                        continue

                    def addIndexedOrNamedOperation(operation, m):
                        self.proxy = True
                        if m.isIndexed():
                            operation = 'Indexed' + operation
                        else:
                            assert m.isNamed()
                            operation = 'Named' + operation
                        addOperation(operation, m)

                    if m.isGetter():
                        addIndexedOrNamedOperation('Getter', m)
                    if m.isSetter():
                        addIndexedOrNamedOperation('Setter', m)
                    if m.isCreator():
                        addIndexedOrNamedOperation('Creator', m)
                    if m.isDeleter():
                        addIndexedOrNamedOperation('Deleter', m)

                iface.setUserData('hasConcreteDescendant', True)
                iface = iface.parent

            if self.proxy:
                if (not operations['IndexedGetter'] and
                    (operations['IndexedSetter'] or
                     operations['IndexedDeleter'] or
                     operations['IndexedCreator'])):
                    raise SyntaxError("%s supports indexed properties but does "
                                      "not have an indexed getter.\n%s" %
                                      (self.interface, self.interface.location))
                if (not operations['NamedGetter'] and
                    (operations['NamedSetter'] or
                     operations['NamedDeleter'] or
                     operations['NamedCreator'])):
                    raise SyntaxError("%s supports named properties but does "
                                      "not have a named getter.\n%s" %
                                      (self.interface, self.interface.location))
                iface = self.interface
                while iface:
                    iface.setUserData('hasProxyDescendant', True)
                    iface = iface.parent
        self.operations = operations

        if self.interface.isExternal() and 'prefable' in desc:
            raise TypeError("%s is external but has a prefable setting" %
                            self.interface.identifier.name)
        self.prefable = desc.get('prefable', False)

        if self.workers:
            if desc.get('nativeOwnership', 'worker') != 'worker':
                raise TypeError("Worker descriptor for %s should have 'worker' "
                                "as value for nativeOwnership" %
                                self.interface.identifier.name)
            self.nativeOwnership = "worker"
        else:
            self.nativeOwnership = desc.get('nativeOwnership', 'nsisupports')
            if not self.nativeOwnership in ['owned', 'refcounted', 'nsisupports']:
                raise TypeError("Descriptor for %s has unrecognized value (%s) "
                                "for nativeOwnership" %
                                (self.interface.identifier.name, self.nativeOwnership))
        self.customTrace = desc.get('customTrace', self.workers)
        self.customFinalize = desc.get('customFinalize', self.workers)
        self.wrapperCache = (not self.interface.isCallback() and
                             (self.workers or
                              (self.nativeOwnership != 'owned' and
                               desc.get('wrapperCache', True))))

        if not self.wrapperCache and self.prefable:
            raise TypeError("Descriptor for %s is prefable but not wrappercached" %
                            self.interface.identifier.name)

        def make_name(name):
            return name + "_workers" if self.workers else name
        self.name = make_name(interface.identifier.name)

        # self.extendedAttributes is a dict of dicts, keyed on
        # all/getterOnly/setterOnly and then on member name. Values are an
        # array of extended attributes.
        self.extendedAttributes = { 'all': {}, 'getterOnly': {}, 'setterOnly': {} }

        def addExtendedAttribute(attribute, config):
            def add(key, members, attribute):
                for member in members:
                    self.extendedAttributes[key].setdefault(member, []).append(attribute)

            if isinstance(config, dict):
                for key in ['all', 'getterOnly', 'setterOnly']:
                    add(key, config.get(key, []), attribute)
            elif isinstance(config, list):
                add('all', config, attribute)
            else:
                assert isinstance(config, str)
                if config == '*':
                    iface = self.interface
                    while iface:
                        add('all', map(lambda m: m.name, iface.members), attribute)
                        iface = iface.parent
                else:
                    add('all', [config], attribute)

        for attribute in ['implicitJSContext', 'resultNotAddRefed']:
            addExtendedAttribute(attribute, desc.get(attribute, {}))

        self.binaryNames = desc.get('binaryNames', {})

        # Build the prototype chain.
        self.prototypeChain = []
        parent = interface
        while parent:
            self.prototypeChain.insert(0, make_name(parent.identifier.name))
            parent = parent.parent
        config.maxProtoChainLength = max(config.maxProtoChainLength,
                                         len(self.prototypeChain))

    def hasInterfaceOrInterfacePrototypeObject(self):

        # Forward-declared interfaces don't need either interface object or
        # interface prototype object as they're going to use QI (on main thread)
        # or be passed as a JSObject (on worker threads).
        if self.interface.isExternal():
            return False

        return self.interface.hasInterfaceObject() or self.interface.hasInterfacePrototypeObject()

    def getExtendedAttributes(self, member, getter=False, setter=False):
        def ensureValidThrowsExtendedAttribute(attr):
            assert(attr is None or attr is True or len(attr) == 1)
            if (attr is not None and attr is not True and
                'Workers' not in attr and 'MainThread' not in attr):
                raise TypeError("Unknown value for 'Throws': " + attr[0])

        def maybeAppendInfallibleToAttrs(attrs, throws):
            ensureValidThrowsExtendedAttribute(throws)
            if (throws is None or
                (throws is not True and
                 ('Workers' not in throws or not self.workers) and
                 ('MainThread' not in throws or self.workers))):
                attrs.append("infallible")

        name = member.identifier.name
        if member.isMethod():
            attrs = self.extendedAttributes['all'].get(name, [])
            throws = member.getExtendedAttribute("Throws")
            maybeAppendInfallibleToAttrs(attrs, throws)
            return attrs

        assert member.isAttr()
        assert bool(getter) != bool(setter)
        key = 'getterOnly' if getter else 'setterOnly'
        attrs = self.extendedAttributes['all'].get(name, []) + self.extendedAttributes[key].get(name, [])
        throws = member.getExtendedAttribute("Throws")
        if throws is None:
            throwsAttr = "GetterThrows" if getter else "SetterThrows"
            throws = member.getExtendedAttribute(throwsAttr)
        maybeAppendInfallibleToAttrs(attrs, throws)
        return attrs

    def supportsIndexedProperties(self):
        return self.operations['IndexedGetter'] is not None

    def supportsNamedProperties(self):
        return self.operations['NamedGetter'] is not None

    def needsConstructHookHolder(self):
        assert self.interface.hasInterfaceObject()
        return not self.hasInstanceInterface and not self.interface.isCallback()
