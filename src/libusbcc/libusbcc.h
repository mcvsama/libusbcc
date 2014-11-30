/* vim:ts=4
 *
 * Copyleft 2014…2015  Michał Gawron
 * Marduk Unix Labs, http://mulabs.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Visit http://www.gnu.org/licenses/gpl-3.0.html for more information on licensing.
 */

#ifndef MULABS_ORG__LIBUSBCC__LIBUSBCC_H__INCLUDED
#define MULABS_ORG__LIBUSBCC__LIBUSBCC_H__INCLUDED

// Standard:
#include <cstddef>
#include <stdexcept>
#include <vector>
#include <memory>

// Lib:
#include <libusb.h>
#include <boost/optional.hpp>

// TinyIO:
#include <tinyio/config/all.h>


namespace libusb {

namespace low_level {

/**
 * RAII-style class for libusb_get_device_list() + libusb_free_device_list().
 */
class DeviceList
{
  public:
	/**
	 * Ctor
	 * May throw StatusException.
	 */
	explicit DeviceList (libusb_context*);

	// Dtor
	~DeviceList();

	/**
	 * Access the list elements.
	 */
	libusb_device*
	operator[] (std::size_t index) const noexcept;

	/**
	 * Return size of the list.
	 */
	std::size_t
	size() const noexcept;

	/**
	 * Return first element iterator.
	 */
	libusb_device**
	begin() const noexcept;

	/**
	 * Return after-the-last element iterator.
	 */
	libusb_device**
	end() const noexcept;

  private:
	libusb_device**	_list;
	std::size_t		_size;
};


inline libusb_device*
DeviceList::operator[] (std::size_t index) const noexcept
{
	return _list[index];
}


inline std::size_t
DeviceList::size() const noexcept
{
	return _size;
}


inline libusb_device**
DeviceList::begin() const noexcept
{
	return _list;
}


inline libusb_device**
DeviceList::end() const noexcept
{
	return _list + _size;
}

} // namespace low_level


class Device;
class DeviceDescriptor;
class Bus;


template<class T>
    using Optional = boost::optional<T>;

template<class T>
	using Shared = std::shared_ptr<T>;


enum class USBVersion: uint16_t
{
	V_1_1	= 0x0110,
	V_2_0	= 0x0200,
	V_3_0	= 0x0300,
};


enum class DeviceClass: uint8_t
{
	PerInterface	= LIBUSB_CLASS_PER_INTERFACE,
};


enum class DeviceSubClass: uint8_t
{
	// TODO
};


enum class DeviceProtocol: uint8_t
{
	// TODO
};


typedef uint16_t VendorID;
typedef uint16_t ProductID;


class Exception: public std::runtime_error
{
	using runtime_error::runtime_error;
};


/**
 * Thrown from a function when error is detected.
 */
class StatusException: public Exception
{
  public:
	// Ctor
	explicit StatusException (libusb_error);

	libusb_error
	status() const noexcept
	{
		return _status;
	}

  private:
	libusb_error _status;
};


/**
 * Thrown when result of a function is not available.
 */
class UnavailableException: public Exception
{
  public:
	// Ctor
	explicit UnavailableException();
};


/**
 * Encapsulates USB control transfers.
 */
class ControlTransfer
{
  public:
	// Ctor
	ControlTransfer (uint8_t request, uint16_t value, uint16_t index);

  public:
	uint8_t		request	= 0;
	uint16_t	value	= 0;
	uint16_t	index	= 0;
};


/**
 * Represents an opened USB device.
 * All Devices must be deleted before Bus is deleted.
 */
class Device
{
  public:
	/**
	 * Ctor
	 * References libusb device in ctor, unreferences it in dtor.
	 *
	 * \param	descriptor
	 * 			DeviceDescriptor object related to this Device object.
	 * \param	libusb_device_handle
	 * 			A pointer to libusb device handle. Must not be null.
	 */
	explicit Device (DeviceDescriptor const&, libusb_device_handle*);

	Device (Device const&) = delete;

	Device (Device&&);

	// Dtor
	~Device();

	Device&
	operator= (Device const&) = delete;

	Device&
	operator= (Device&&);

	/**
	 * Return DeviceDescriptor for this device.
	 */
	DeviceDescriptor const&
	descriptor() const noexcept;

	/**
	 * Return device's iManufacturer.
	 */
	std::string
	manufacturer() const;

	/**
	 * Return device's iProduct.
	 */
	std::string
	product() const;

	/**
	 * Return serial number.
	 */
	std::string
	serial_number() const;

	/**
	 * Make a synchronous control transfer to the device.
	 *
	 * \param	timeout_ms
	 * 			Timeout in milliseconds. 0 means unlimited timeout.
	 */
	void
	send (ControlTransfer const&, int timeout_ms = 0, std::vector<uint8_t> const& buffer = {});

	/**
	 * Make a synchronous control transfer from the device.
	 *
	 * \param	timeout_ms
	 * 			Timeout in milliseconds. 0 means unlimited timeout.
	 */
	std::vector<uint8_t>
	receive (ControlTransfer const& ct, int timeout_ms = 0);

  private:
	/**
	 * Empty the object (destructor will do nothing).
	 */
	void
	reset();

	/**
	 * Close device if it was open, unreference the device.
	 * Use when destroying or moving-out.
	 */
	void
	cleanup();

	/**
	 * Return string for given text ID in libusb_device_descriptor.
	 * (eg. iManufacturer, iProduct).
	 */
	std::string
	get_usb_string (int string_id) const;

  private:
	// Can be nullptr after a move-out:
	Shared<DeviceDescriptor>		_descriptor;
	libusb_device_handle*			_handle		= nullptr;
};


/**
 * Represents a USB device.
 * To open a device, obtain a Device object by calling open().
 */
class DeviceDescriptor
{
	friend class Device;

  public:
	// Ctor
	explicit DeviceDescriptor (libusb_device*);

	DeviceDescriptor (DeviceDescriptor const&);

	DeviceDescriptor (DeviceDescriptor&&);

	// Dtor
	~DeviceDescriptor();

	DeviceDescriptor&
	operator= (DeviceDescriptor const&);

	DeviceDescriptor&
	operator= (DeviceDescriptor&&);

	/**
	 * Opens device and returns a Device object.
	 */
	Device
	open() const;

	/**
	 * Return the number of the bus that a device is connected to.
	 */
	uint8_t
	bus_id() const noexcept;

	/**
	 * Return the number of the port that a device is connected to.
	 * Value of 0 means the port number is not available.
	 */
	uint8_t
	port_id() const noexcept;

	/**
	 * Get the parent from the specified device.
	 */
	DeviceDescriptor
	parent (Bus const&) const;

	/**
	 * Return USB version.
	 */
	USBVersion
	usb_version() const;

	/**
	 * Return USB version as human-readable string.
	 */
	const char*
	usb_version_str() const;

	/**
	 * Device version.
	 */
	uint16_t
	release_version() const;

	/**
	 * Device version as human-readable string.
	 */
	std::string
	release_version_str() const;

	/**
	 * Return device's vendor ID.
	 */
	VendorID
	vendor_id() const;

	/**
	 * Return device's product ID.
	 */
	ProductID
	product_id() const;

	/**
	 * Return USB class of the device (bDeviceClass).
	 */
	DeviceClass
	usb_class() const;

	/**
	 * Return USB sub-class of the device (bDeviceSubClass).
	 */
	DeviceSubClass
	usb_sub_class() const;

	/**
	 * Return USB protocol (bDeviceProtocol).
	 */
	DeviceProtocol
	usb_protocol() const;

	/**
	 * Return number of configurations of the device.
	 */
	uint8_t
	num_configurations() const;

	/**
	 * Return max. packet size for configuration 0.
	 */
	uint8_t
	max_packet_size_0() const;

  private:
	/**
	 * Empty the object (destructor will do nothing).
	 */
	void
	reset();

	/**
	 * Unreference the device.
	 * Use when destroying or moving-out.
	 */
	void
	cleanup();

	/**
	 * Return device descriptor. If not obtained, obtain it.
	 */
	libusb_device_descriptor&
	descriptor() const;

  private:
	libusb_device*								_device;
	Optional<libusb_device_descriptor> mutable	_descriptor;
};


typedef std::vector<DeviceDescriptor> DeviceDescriptors;


/**
 * Represents libusb session.
 * http://libusb.sourceforge.net/api-1.0/contexts.html
 */
class Bus
{
  public:
	// Ctor
	Bus();

	// Dtor
	~Bus();

	/**
	 * Return libusb context pointer.
	 */
	libusb_context*
	get_libusb_context() const noexcept;

	/**
	 * Return list of devices detected in the system.
	 */
	DeviceDescriptors
	device_descriptors() const;

  private:
	libusb_context* _context;
};


inline libusb_context*
Bus::get_libusb_context() const noexcept
{
	return _context;
}


/**
 * Return true if an int returned by libusb function
 * is an error status code.
 */
bool
is_error (int status);

} // namespace libusb

#endif

