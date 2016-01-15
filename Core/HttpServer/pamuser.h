/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2015  Enrico Reimer <email>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef PAMUSER_H
#define PAMUSER_H

#include <boost/noncopyable.hpp>
#include <security/pam_appl.h>
#include <string>
#include <set>

class PamUser : boost::noncopyable{
	pam_handle_t *m_auth_handle;
	pam_conv m_conversation;
	std::string m_username;
	static int function_conversation(int num_msg, const pam_message **msg, pam_response **resp, void *appdata_ptr);
public:
	PamUser(const std::string &username);
	bool auth(const std::string &password);
	bool hasGroup(const std::set< std::string >& lookup);
	~PamUser();
};

#endif // PAMUSER_H
