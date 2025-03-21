/*
	Copyright (C) 2011 - 2025
	by Sytyi Nick <nsytyi@gmail.com>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

/**
 * @file validator.hpp
 * This file contains information about validation abstract level interface.
 */

#pragma once

#include "exceptions.hpp"

#include <string>

class config;
class config_attribute_value;
extern bool strict_validation_enabled;

/**
 * @class abstract_validator
 * Used in parsing config file. @ref parser.cpp
 * Contains virtual methods, which are called by parser
 * and take information about config to be validated
 */
class abstract_validator
{
public:
	/**
	 * Constructor of validator can throw validator::error
	 * @throws abstract_validator::error
	 */
	abstract_validator(const std::string& name) : name_(name) {}

	virtual ~abstract_validator(){}
	/**
	 * Is called when parser opens tag.
	 * @param name        Name of tag
	 * @param parent      The parent config
	 * @param start_line  Line in file
	 * @param file        Name of file
	 * @param addition
	 */
	virtual void open_tag(const std::string & name,
						  const config& parent,
						  int start_line,
						  const std::string &file,
						  bool addition = false) = 0;
	/**
	 * As far as parser is built on stack, some realizations can store stack
	 * too. So they need to know if tag was closed.
	 */
	virtual void close_tag() = 0;
	/**
	 * Validates config. Checks if all mandatory elements are present.
	 * What exactly is validated depends on validator realization
	 * @param cfg         Config to be validated.
	 * @param name        Name of tag
	 * @param start_line  Line in file
	 * @param file        Name of file
	 */
	virtual void validate(const config & cfg,
						  const std::string & name,
						  int start_line,
						  const std::string &file) = 0;
	/**
	 * Checks if key is allowed and if its value is valid
	 * What exactly is validated depends on validator realization
	 * @param cfg         Config to be validated.
	 * @param name        Name of tag
	 * @param value       The key's value
	 * @param start_line  Line in file
	 * @param file        Name of file
	 */

	virtual void validate_key(const config & cfg,
							  const std::string & name,
							  const config_attribute_value & value,
							  int start_line,
							  const std::string &file) = 0;
	/**
	 * @struct error
	 * Used to manage with not initialized validators
	 * Supposed to be thrown from the constructor
	 */
	struct error : public game::error {
		error(const std::string& message) : game::error(message) {}
	};

	const std::string name_;
};
