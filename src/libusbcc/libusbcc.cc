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

// Lib:
#include <exception>
#include <libusb.h>

// Local:
#include "libusbcc.h"


namespace libusb {

namespace low_level {

DeviceList::DeviceList (libusb_context* context)
{
	_size = libusb_get_device_list (context, &_list);

	if (is_error (_size))
		throw StatusException (static_cast<libusb_error> (_size));
}


DeviceList::~DeviceList()
{
	libusb_free_device_list (_list, 1);
}

} // namespace low_level


StatusException::StatusException (libusb_error code):
	Exception (libusb_strerror (code)),
	_status (code)
{ }


UnavailableException::UnavailableException():
	Exception ("result unavailable")
{ }


ControlTransfer::ControlTransfer (uint8_t request, uint16_t value, uint16_t index):
	request (request),
	value (value),
	index (index)
{ }


Device::Device (DeviceDescriptor const& descriptor, libusb_device_handle* handle):
	_descriptor (std::make_shared<DeviceDescriptor> (descriptor)),
	_handle (handle)
{ }


Device::~Device()
{
	cleanup();
}


Device::Device (Device&& other):
	_descriptor (other._descriptor),
	_handle (other._handle)
{
	other.reset();
}


Device&
Device::operator= (Device&& other)
{
	cleanup();
	_descriptor = other._descriptor;
	_handle = other._handle;
	other.reset();
	return *this;
}


DeviceDescriptor const&
Device::descriptor() const noexcept
{
	return *_descriptor;
}


std::string
Device::manufacturer() const
{
	return get_usb_string (_descriptor->descriptor().iManufacturer);
}


std::string
Device::product() const
{
	return get_usb_string (_descriptor->descriptor().iProduct);
}


std::string
Device::serial_number() const
{
	return get_usb_string (_descriptor->descriptor().iSerialNumber);
}


void
Device::send (ControlTransfer const& ct, int timeout_ms, std::vector<uint8_t> const& buffer)
{
	// For to-device transfers we can assume that buffer will not change.
	// Therefore allow const_cast to make C function happy.
	auto ll_buffer = const_cast<uint8_t*> (buffer.data());
	int bytes_transferred = libusb_control_transfer (_handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
													 ct.request, ct.value, ct.index, ll_buffer, buffer.size(), timeout_ms);
	if (is_error (bytes_transferred))
		throw StatusException (static_cast<libusb_error> (bytes_transferred));
}


std::vector<uint8_t>
Device::receive (ControlTransfer const& ct, int timeout_ms)
{
	// Control transfers may carry up to 64 bytes of data, so allocate a vector of 64 bytes:
	std::vector<uint8_t> buffer (64, 0);
	int bytes_transferred = libusb_control_transfer (_handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN,
													 ct.request, ct.value, ct.index, buffer.data(), buffer.size(), timeout_ms);
	if (is_error (bytes_transferred))
		throw StatusException (static_cast<libusb_error> (bytes_transferred));
	buffer.resize (bytes_transferred);
	return buffer;
}


inline void
Device::reset()
{
	_handle = nullptr;
}


inline void
Device::cleanup()
{
	if (_handle)
		libusb_close (_handle);
}


std::string
Device::get_usb_string (int string_id) const
{
	// TODO what about UTF-16
	if (string_id > 0)
	{
		char buffer[256];
		int chars = libusb_get_string_descriptor_ascii (_handle, string_id, reinterpret_cast<unsigned char*> (buffer), sizeof (buffer));
		if (chars < 0)
			throw StatusException (static_cast<libusb_error> (chars));
		buffer[chars] = 0;
		return std::string (buffer);
	}
	else
		return std::string();
}


DeviceDescriptor::DeviceDescriptor (libusb_device* device):
	_device (device)
{
	libusb_ref_device (_device);
}


DeviceDescriptor::DeviceDescriptor (DeviceDescriptor const& other):
	_device (other._device)
{
	libusb_ref_device (_device);
}


DeviceDescriptor::DeviceDescriptor (DeviceDescriptor&& other):
	_device (other._device)
{
	other.reset();
}


DeviceDescriptor::~DeviceDescriptor()
{
	cleanup();
}


DeviceDescriptor&
DeviceDescriptor::operator= (DeviceDescriptor const& other)
{
	cleanup();
	_device = other._device;
	libusb_ref_device (_device);
	return *this;
}


DeviceDescriptor&
DeviceDescriptor::operator= (DeviceDescriptor&& other)
{
	cleanup();
	_device = other._device;
	other.reset();
	return *this;
}


Device
DeviceDescriptor::open() const
{
	libusb_device_handle* handle;
	int err = libusb_open (_device, &handle);
	if (is_error (err))
		throw StatusException (static_cast<libusb_error> (err));
	return Device (*this, handle);
}


uint8_t
DeviceDescriptor::bus_id() const noexcept
{
	return libusb_get_bus_number (_device);
}


uint8_t
DeviceDescriptor::port_id() const noexcept
{
	return libusb_get_port_number (_device);
}


DeviceDescriptor
DeviceDescriptor::parent (Bus const& bus) const
{
	// Needed for libusb_get_parent:
	low_level::DeviceList devices (bus.get_libusb_context());
	// This requires DeviceList to be present (needs to be called
	// between libusb_get_device_list() and libusb_free_device_list():
	libusb_device* parent_device = libusb_get_parent (_device);

	if (parent_device)
		return DeviceDescriptor (parent_device);
	else
		throw UnavailableException();
}


USBVersion
DeviceDescriptor::usb_version() const
{
	return static_cast<USBVersion> (descriptor().bcdUSB);
}


const char*
DeviceDescriptor::usb_version_str() const
{
	switch (usb_version())
	{
		case USBVersion::V_1_1:
			return "1.1";
		case USBVersion::V_2_0:
			return "2.0";
		case USBVersion::V_3_0:
			return "3.0";
		default:
			return "unknown";
	}
}


uint16_t
DeviceDescriptor::release_version() const
{
	return descriptor().bcdDevice;
}


std::string
DeviceDescriptor::release_version_str() const
{
	uint16_t v = release_version();
	return std::to_string ((v >> 8) & 0xff) + '.' + std::to_string (v & 0xff);
}


VendorID
DeviceDescriptor::vendor_id() const
{
	return descriptor().idVendor;
}


ProductID
DeviceDescriptor::product_id() const
{
	return descriptor().idProduct;
}


DeviceClass
DeviceDescriptor::usb_class() const
{
	return static_cast<DeviceClass> (descriptor().bDeviceClass);
}


DeviceSubClass
DeviceDescriptor::usb_sub_class() const
{
	return static_cast<DeviceSubClass> (descriptor().bDeviceSubClass);
}


DeviceProtocol
DeviceDescriptor::usb_protocol() const
{
	return static_cast<DeviceProtocol> (descriptor().bDeviceProtocol);
}


uint8_t
DeviceDescriptor::num_configurations() const
{
	return descriptor().bNumConfigurations;
}


uint8_t
DeviceDescriptor::max_packet_size_0() const
{
	return descriptor().bMaxPacketSize0;
}


inline libusb_device_descriptor&
DeviceDescriptor::descriptor() const
{
	if (_descriptor)
		return *_descriptor;
	else
	{
		_descriptor.emplace (libusb_device_descriptor());
		int err = libusb_get_device_descriptor (_device, _descriptor.get_ptr());
		if (is_error (err))
			throw StatusException (static_cast<libusb_error> (err));
		return *_descriptor;
	}
}


inline void
DeviceDescriptor::reset()
{
	_device = nullptr;
}


inline void
DeviceDescriptor::cleanup()
{
	if (_device)
		libusb_unref_device (_device);
}


Bus::Bus()
{
	try {
		int err = libusb_init (&_context);
		if (is_error (err))
			throw StatusException (static_cast<libusb_error> (err));
	}
	catch (...)
	{
		std::throw_with_nested (Exception ("failed to create libusb session"));
	}
}


Bus::~Bus()
{
	libusb_exit (_context);
}


DeviceDescriptors
Bus::device_descriptors() const
{
	DeviceDescriptors result;

	try {
		low_level::DeviceList devices (_context);

		// Build list of Devices:
		for (auto const& d: devices)
			result.emplace_back (d);
	}
	catch (...)
	{
		std::throw_with_nested (Exception ("failed to get device list"));
	}

	return result;
}


bool
is_error (int status)
{
	switch (status)
	{
		case LIBUSB_SUCCESS:
			return false;

		case LIBUSB_ERROR_IO:
		case LIBUSB_ERROR_INVALID_PARAM:
		case LIBUSB_ERROR_ACCESS:
		case LIBUSB_ERROR_NO_DEVICE:
		case LIBUSB_ERROR_NOT_FOUND:
		case LIBUSB_ERROR_BUSY:
		case LIBUSB_ERROR_TIMEOUT:
		case LIBUSB_ERROR_OVERFLOW:
		case LIBUSB_ERROR_PIPE:
		case LIBUSB_ERROR_INTERRUPTED:
		case LIBUSB_ERROR_NO_MEM:
		case LIBUSB_ERROR_NOT_SUPPORTED:
		case LIBUSB_ERROR_OTHER:
			return true;

		default:
			return false;
	}
}

} // namespace libusb

