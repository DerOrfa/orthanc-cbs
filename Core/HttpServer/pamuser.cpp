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

#include "pamuser.h"
#include <pwd.h>
#include <grp.h>
#include <string>
#include <boost/scoped_array.hpp>
#include "../Logging.h"

int PamUser::function_conversation(int num_msg, const pam_message** msg, pam_response** resp, void* appdata_ptr){
	*resp = (struct pam_response *)malloc(sizeof(pam_response));
	(*resp)->resp = (char*)appdata_ptr;
	(*resp)->resp_retcode = 0;
	return PAM_SUCCESS;
}

PamUser::PamUser(const std::string &username):m_auth_handle(NULL),m_username(username){
	m_conversation.conv = function_conversation;
}


bool PamUser::auth(const std::string &password){
	m_conversation.appdata_ptr=malloc(password.size()+1);
	strcpy((char*)m_conversation.appdata_ptr,password.c_str());
	int m_status=pam_start("su", m_username.c_str(), &m_conversation, &m_auth_handle);
	if(m_status==PAM_SUCCESS){
		m_status = pam_authenticate(m_auth_handle, 0);
	}
	if (m_status != PAM_SUCCESS)	{
		if (m_status == PAM_AUTH_ERR)
			LOG(WARNING) << "Authentication failure for " << m_username;
		else 
			switch(m_status){
			case PAM_ABORT:
				LOG(ERROR) << "PAM_ABORT when authenticating " << m_username;break;
			case PAM_CRED_INSUFFICIENT:
				LOG(ERROR) << "PAM_CRED_INSUFFICIENT when authenticating " << m_username;break;
			case PAM_MAXTRIES:
				LOG(ERROR) << "PAM_MAXTRIES when authenticating " << m_username;break;
			case PAM_USER_UNKNOWN:
				LOG(ERROR) << "PAM_USER_UNKNOWN when authenticating " << m_username;break;
			default:
				LOG(ERROR) << "Unknown pam error " << m_status << " when authenticating " << m_username;break;
			}
			
		
		return false;
	} else 
		return pam_acct_mgmt(m_auth_handle, 0)==PAM_SUCCESS;
}

bool PamUser::hasGroup(const std::set< std::string >& lookup){
	passwd* pw=getpwnam(m_username.c_str());
	std::set<std::string> ret;
	if(pw==NULL){
		LOG(WARNING) << "Failed to get userinfo for " << m_username;
		return false;
	}
	
	// figure out how many groups there are
	int ngroups=0;
	getgrouplist(m_username.c_str(), pw->pw_gid, NULL, &ngroups); 
	
	// and now for real
	boost::scoped_array<gid_t> groups(new gid_t[ngroups]);
	getgrouplist(m_username.c_str(), pw->pw_gid, groups.get(), &ngroups); 
	
	for (int j = 0; j < ngroups; j++) {
		const group *const gr = getgrgid(groups[j]);
		if (gr != NULL){
			if(lookup.find(gr->gr_name)!=lookup.end())
				return true;
		} else {
			LOG(WARNING) << "Huh? no name for group id " << groups[j];
		}
	}
	return false;
}

PamUser::~PamUser(){
	int m_status;
	pam_end(m_auth_handle, m_status);
}
